# NorseTagger

KID-like tags for NORSE: Oath of Blood - a UE4SS v4.0.0 C++ mod. Norse's items and abilities have no general tag or
keyword list (only passives and units carry gameplay-tag "special effects"), so NorseTagger keeps its own: rules give
tags to loaded data assets - items, unit types, abilities - and other frameworks filter on them with `Tag:Name`.
Together with NorseDistributor it is Skyrim's KID + SPID pair: tag things, then distribute by tag.

From the framework list in `4. plans\norse-realtime-combat-20261116\ASSESSMENT-AND-PLAN.md` ("NorseTagger (KID-like)").

## Rules

`ue4ss\Mods\NorseTagger\tags\*.ini`, in NorseManifest's pack order:

```ini
Tag = HeavyWeapon | ItemAsset_HeavyAxe, ItemAsset_Spear
Tag = OneHanded   | ItemAsset_Weapon_OneHanded          ; a parent class covers every axe, sword and knife
Tag = Brigand     | CombatUnit | | | Profiles:Brigands   ; by a field's content (the faction)
Tag = Basic       | CombatUnit | Basic_*, -Basic_Archer
```

`Tag = name | classes | names | folders | field filters | chance` - author guide `package\tags\README.txt`, tested
examples `docs\examples\`. Using a tag elsewhere (NorseDistributor):
`ActiveAbilities = /Game/.../ShieldSlam_Enemy.ShieldSlam_Enemy | | | Tag:Brigand`.

## For other frameworks

Exports `int NT_HasTag(void* obj, const char* tag)` and `void NT_Apply()` (tag this map's assets now). The shared
`../NorsePatcher/include/NorseFilters.hpp` finds them and answers `Tag:` filters, and calls `NT_Apply` first, so the
order the mods load in does not matter. Console: `nt.list <tag>`, `nt.tags <object path>`, `nt.reload`, `nt.apply`.

Not yet: real gameplay tags written into the game's own containers (passive SpecialEffects) - tags here are
NorseTagger's own. Applies to assets loaded when the map loads.

## Build

`xmake f -p windows -a x64 -m release --toolchain=msvc -y`, then `xmake` (needs `../NorsePatcher` and `../NorseManifest`).

## Licence

GPL-3.0-or-later (see LICENSE).
