NorseTagger rule files
======================

Every *.ini file in this folder is read (in NorseManifest's pack order). Each rule gives a tag to every loaded object of
the named classes that passes its filters. Tags are NorseTagger's own - nothing in the game's files changes - and other
frameworks filter on them with Tag:Name (NorseDistributor: in the field-filter part of a rule).

One rule per line; everything after the classes is optional:

    Tag = name | classes | names | folders | field filters | chance

    Tag = HeavyWeapon | ItemAsset_HeavyAxe, ItemAsset_Spear
    Tag = OneHanded   | ItemAsset_Weapon_OneHanded          ; a parent class covers every axe, sword and knife
    Tag = Brigand     | CombatUnit | | | Profiles:Brigands   ; by a field's content (the faction)
    Tag = Basic       | CombatUnit | Basic_*, -Basic_Archer

classes  the object's class or any parent class, as Unreal names it without the U/A prefix (required).
names    the object's name; * wildcards; -name excludes.
folders  text in the object's path; -text excludes.
fields   Field:text (that field's value contains text), Tag:Name (already tagged by an earlier rule); -excludes.
chance   0-100, a fixed roll per object and rule.

Console: nt.list <tag> (what has it), nt.tags <object path> (an object's tags), nt.reload, nt.apply.
The log is NorseTagger.log beside NorseTagger.ini: a summary per rule; uLogLevel=1 lists every tag given.
