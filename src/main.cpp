// NorseTagger - KID-like tags for NORSE: Oath of Blood (UE4SS v4.0.0 C++ mod), 2026-11-17.
//
// Norse's items and abilities carry no general tag or keyword list (only passives and units have gameplay-tag "special
// effects"), so NorseTagger keeps its own: rules give tags to loaded data assets - items, unit types, abilities - by
// class (or any parent class), name, folder, field content and chance. Other frameworks ask through two exports:
//   int  NT_HasTag(void* obj, const char* tag)   1 when obj has the tag (case-insensitive)
//   void NT_Apply()                               (re)tag this map's assets now
// NorseDistributor (and every framework using NorseFilters.hpp) accepts Tag:Name in its field filters.
//
// Rule files: <this mod>\tags\*.ini, in NorseManifest's load order. One rule per line:
//   Tag = <tag> | <classes> | <names> | <folders> | <field filters> | <chance>
//   Tag = HeavyWeapon | ItemAsset_HeavyAxe, ItemAsset_Spear
//   Tag = Brigand | CombatUnit | | | Profiles:Brigands
// classes is required (the classes whose loaded objects are looked at). Applied at every map load and by nt.reload /
// nt.apply; nt.list <tag> logs what has it; nt.tags <object path> logs an object's tags.
#include "NorseReflect.hpp"
#include "NorseManifest.hpp"
#include "NorseFilters.hpp"

#include <map>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace nrn;
using namespace nrn::ue;
using namespace norse;

namespace
{
    struct Config
    {
        int LogLevel = 2;
        bool ApplyOnMapLoad = true;
        std::wstring LogDir;
    } g_cfg;
    fs::path g_mod, g_out;

    fs::path ModDir()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModDir), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
        return fs::path(buf).parent_path().parent_path();
    }

    void LoadConfig(const fs::path& ini)
    {
        g_cfg = Config{};
        for (const auto& raw : ReadLines(ini))
        {
            std::wstring l = Trim(StripComment(raw));
            size_t eq = l.find(L'=');
            if (l.empty() || l[0] == L'[' || eq == std::wstring::npos) continue;
            std::wstring k = Trim(l.substr(0, eq)), v = Trim(l.substr(eq + 1));
            if (k == L"uLogLevel") g_cfg.LogLevel = _wtoi(v.c_str());
            else if (k == L"bApplyOnMapLoad") g_cfg.ApplyOnMapLoad = _wtoi(v.c_str()) != 0;
            else if (k == L"sLogDir") g_cfg.LogDir = v;
        }
    }

    struct Rule
    {
        std::wstring tag; // as written
        Filters f;
        std::string where;
    };
    std::vector<Rule> g_rules;
    std::unordered_map<void*, std::set<std::wstring>> g_tags; // object -> lower-case tags
    std::map<std::wstring, std::wstring> g_display;            // lower-case tag -> as first written

    void LoadRules()
    {
        g_rules.clear();
        fs::path dir = g_mod / "tags";
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_directory(dir, ec))
            for (const auto& f : fs::directory_iterator(dir, ec))
                if (f.is_regular_file() && Lower(f.path().extension().wstring()) == L".ini") files.push_back(f.path());
        {
            nm::Manifest man;
            man.Resolve(g_mod.parent_path().parent_path());
            files = man.Arrange(files, [](const std::string& s) { NRN_INFO("load order: %s", s.c_str()); });
        }
        for (const auto& f : files)
        {
            int n = 0;
            for (const auto& raw : ReadLines(f))
            {
                ++n;
                std::wstring l = Trim(StripComment(raw));
                if (l.empty() || l[0] == L'[') continue;
                std::string where = Narrow(f.filename().wstring()) + ":" + std::to_string(n);
                size_t eq = l.find(L'=');
                if (eq == std::wstring::npos || _wcsicmp(Trim(l.substr(0, eq)).c_str(), L"Tag") != 0)
                {
                    NRN_WARN("%s: not a 'Tag = name | classes | names | folders | fields | chance' line - skipped", where.c_str());
                    continue;
                }
                auto parts = SplitList(l.substr(eq + 1), L'|');
                Rule r;
                r.where = where;
                r.tag = parts[0];
                if (parts.size() > 1) r.f.classes = ParseFilter(parts[1]);
                if (parts.size() > 2) r.f.names = ParseFilter(parts[2]);
                if (parts.size() > 3) r.f.folders = ParseFilter(parts[3]);
                if (parts.size() > 4) r.f.fields = ParseFields(parts[4], where);
                if (parts.size() > 5) r.f.chance = ParseChance(parts[5]);
                if (r.tag.empty() || r.f.classes.inc.empty())
                {
                    NRN_WARN("%s: a tag rule needs a tag name and at least one class - skipped", where.c_str());
                    continue;
                }
                g_display.emplace(Lower(r.tag), r.tag);
                g_rules.push_back(std::move(r));
            }
        }
        NRN_INFO("rules: %zu file(s) in %s, %zu rule(s)", files.size(), Narrow(dir.wstring()).c_str(), g_rules.size());
    }

    bool g_applying = false;

    void ApplyAll(const char* why)
    {
        if (g_applying) return;
        g_applying = true;
        g_tags.clear();
        std::map<std::wstring, std::vector<UObject*>> byClass;
        size_t total = 0;
        for (const Rule& r : g_rules)
        {
            std::set<UObject*> seen;
            int tagged = 0, filtered = 0;
            for (const auto& c : r.f.classes.inc)
            {
                auto& objs = byClass[c];
                if (objs.empty())
                {
                    try
                    {
                        RC::Unreal::UObjectGlobals::FindAllOf(c.c_str(), objs);
                    }
                    catch (...)
                    {
                    }
                }
                for (UObject* obj : objs)
                {
                    if (!seen.insert(obj).second) continue;
                    std::wstring full = obj->GetFullName(nullptr);
                    std::wstring path = full.substr(full.find(L' ') + 1);
                    std::wstring name = ShortName(path);
                    if (name.rfind(L"Default__", 0) == 0) continue;
                    auto* cls = reinterpret_cast<UStruct*>(Reflect::Get().ClassOf(obj));
                    if (!cls) continue;
                    if (!Rejects(r.f, obj, cls, name, path, name + L"|" + r.tag).empty())
                    {
                        ++filtered;
                        continue;
                    }
                    if (g_tags[obj].insert(Lower(r.tag)).second)
                    {
                        ++tagged;
                        ++total;
                        NRN_DEBUG("%s: %s +%s", r.where.c_str(), Narrow(name).c_str(), Narrow(r.tag).c_str());
                    }
                }
            }
            NRN_INFO("%s: Tag %s -> %d object(s) tagged, %d filtered out (%zu looked at)", r.where.c_str(), Narrow(r.tag).c_str(), tagged,
                     filtered, seen.size());
        }
        NRN_INFO("tagged (%s): %zu tag(s) on %zu object(s) from %zu rule(s)", why, total, g_tags.size(), g_rules.size());
        g_applying = false;
    }

    void List(const std::wstring& tag)
    {
        std::wstring t = Lower(tag);
        std::vector<std::string> names;
        for (auto& [obj, tags] : g_tags)
            if (tags.count(t)) names.push_back(Narrow(ShortName(static_cast<UObject*>(obj)->GetFullName(nullptr))));
        std::sort(names.begin(), names.end());
        NRN_INFO("nt.list %s: %zu object(s)", Narrow(tag).c_str(), names.size());
        for (auto& n : names) NRN_INFO("  %s", n.c_str());
    }

    void TagsOf(const std::wstring& path)
    {
        UObject* obj = nullptr;
        try
        {
            obj = RC::Unreal::UObjectGlobals::StaticFindObject_InternalSlow(nullptr, nullptr, path.c_str(), false);
        }
        catch (...)
        {
        }
        if (!obj)
        {
            NRN_WARN("nt.tags: %s is not loaded", Narrow(path).c_str());
            return;
        }
        std::string s;
        if (auto it = g_tags.find(obj); it != g_tags.end())
            for (auto& t : it->second) s += (s.empty() ? "" : ", ") + Narrow(g_display.count(t) ? g_display[t] : t);
        NRN_INFO("nt.tags %s: %s", Narrow(ShortName(path)).c_str(), s.empty() ? "(none)" : s.c_str());
    }

    bool RunCommand(const std::wstring& c)
    {
        std::wstring cmd = Trim(c), arg;
        if (size_t sp = cmd.find(L' '); sp != std::wstring::npos)
        {
            arg = Trim(cmd.substr(sp + 1));
            cmd = cmd.substr(0, sp);
        }
        cmd = Lower(cmd);
        NRN_INFO("command: %s %s", Narrow(cmd).c_str(), Narrow(arg).c_str());
        if (cmd == L"nt.reload")
        {
            LoadRules();
            ApplyAll("nt.reload");
        }
        else if (cmd == L"nt.apply") ApplyAll("nt.apply");
        else if (cmd == L"nt.list" && !arg.empty()) List(arg);
        else if (cmd == L"nt.tags" && !arg.empty()) TagsOf(arg);
        else NRN_WARN("unknown command - nt.reload | nt.apply | nt.list <tag> | nt.tags <object path>");
        return true;
    }
} // namespace

extern "C" __declspec(dllexport) int NT_HasTag(void* obj, const char* tag)
{
    if (!obj || !tag) return 0;
    auto it = g_tags.find(obj);
    return it != g_tags.end() && it->second.count(Lower(nm::Widen(tag))) ? 1 : 0;
}

extern "C" __declspec(dllexport) void NT_Apply()
{
    ApplyAll("asked by another framework");
}

class NorseTagger : public RC::CppUserModBase
{
  public:
    NorseTagger()
    {
        ModName = L"NorseTagger";
        ModVersion = L"" NT_VERSION;
        ModDescription = L"KID-like tags: rules tag items, unit types and abilities; other frameworks filter on Tag:Name.";
        ModAuthors = L"ApocryphaRealm";
        for (auto& s : g_nopVtbl) s = reinterpret_cast<void*>(&NopFn);
        g_errOut.vtbl = g_nopVtbl;
        g_mod = ModDir();
        LoadConfig(g_mod / "NorseTagger.ini");
        g_out = g_cfg.LogDir.empty() ? g_mod : fs::path(g_cfg.LogDir);
        Log::Get().Open(g_out / "NorseTagger.log", g_cfg.LogLevel);
        Log::Get().Write(Level::Error, "NorseTagger v%s - log level %d", NT_VERSION, g_cfg.LogLevel);
        LoadRules();
    }

    auto on_unreal_init() -> void override
    {
        NRN_INFO("on_unreal_init: registering InitGameState and console callbacks");
        try
        {
            RC::Unreal::Hook::RegisterInitGameStatePostCallback([](RC::Unreal::AGameModeBase*) {
                try
                {
                    if (g_cfg.ApplyOnMapLoad) ApplyAll("map load");
                }
                catch (...)
                {
                    NRN_ERROR("map-load tagging threw");
                }
            });
            RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                    [](UObject*, const wchar_t* cmd, RC::Unreal::FOutputDevice&, UObject*) -> bool {
                        if (!cmd || _wcsnicmp(cmd, L"nt.", 3) != 0) return false;
                        static ULONGLONG last = 0;
                        static std::wstring lastCmd;
                        ULONGLONG now = GetTickCount64();
                        if (lastCmd == cmd && now - last < 250) return true;
                        last = now;
                        lastCmd = cmd;
                        try
                        {
                            return RunCommand(cmd);
                        }
                        catch (...)
                        {
                            NRN_ERROR("command threw");
                            return true;
                        }
                    });
        }
        catch (const std::exception& e)
        {
            NRN_ERROR("callback registration failed: %s", e.what());
        }
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new NorseTagger();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
