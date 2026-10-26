# Forever World Buffs

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that brings WoW Forever's
world buff rule to a 3.3.5 server: **world buffs don't work in raids.**

- Entering a raid **pauses** your world buffs: they come off your buff bar, and their time stops.
- Leaving the raid gives them back with **the time they had left**.

World buffs still help you quest and level, but they can't make a raid easier, so raids can be
balanced around players without them. No client patch.

## Which buffs

| Buff | Spell | Where it comes from |
|------|-------|---------------------|
| Rallying Cry of the Dragonslayer | 22888 | Onyxia's or Nefarian's head |
| Warchief's Blessing | 16609 | Rend Blackhand's head |
| Spirit of Zandalar | 24425 | Heart of Hakkar |
| Songflower Serenade | 15366 | Songflowers in Felwood |
| Fengus' Ferocity, Mol'dar's Moxie, Slip'kik's Savvy | 22817, 22818, 22820 | Dire Maul North tribute run |
| Sayge's Dark Fortunes | 23735-23738, 23766-23769 | Sayge at the Darkmoon Faire |

The list is a setting, so you can add or remove buffs.

## How it behaves

- **Logging out or a server restart inside the raid** keeps the paused buffs. They're saved in the
  characters database and come back when you leave.
- **Dying in the raid** doesn't cost you paused buffs. If you release and come back to life
  outside, they return when you're resurrected.
- **A fresh buff wins.** If you pick up the same buff again before your paused copy comes back, you
  keep whichever has more time left.
- **Bots** follow the same rules as players.
- With `Announce` on, you get a chat line listing what was paused and what came back.

## Requirements

- [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) `master` (WotLK 3.3.5a)
- A WoW 3.3.5a (12340) client
- No client patch. One small table in the characters database (installed automatically).

## Install

Clone it into your AzerothCore `modules` folder **as `mod-forever-world-buffs`**, without the
repo's `wow-` prefix. AzerothCore finds the module's entry point from the folder name.

```bash
cd <azerothcore>/modules
git clone https://github.com/buildthehomelab/wow-mod-forever-world-buffs.git mod-forever-world-buffs
```

Rebuild the worldserver, then copy `conf/mod_forever_world_buffs.conf.dist` to your config folder
as `mod_forever_world_buffs.conf`. The worldserver's database updater creates the
`character_forever_world_buffs` table on its next start.

## Settings

| Setting | Default | What it does |
|---------|---------|--------------|
| `ForeverWorldBuffs.Enable` | `1` | Master switch. With `0`, world buffs work in raids, and paused buffs come back. |
| `ForeverWorldBuffs.IncludeDungeons` | `0` | With `1`, 5-player dungeons pause world buffs too. |
| `ForeverWorldBuffs.Announce` | `1` | Chat line when buffs are paused and when they come back. |
| `ForeverWorldBuffs.Spells` | the list above | Comma-separated spell IDs that count as world buffs. |

`.reload config` picks up changes without a restart. A setting change applies the next time a
player changes map, so anyone already inside a raid keeps their state until then.

## Uninstall

Turn `ForeverWorldBuffs.Enable` off for a while first, so paused buffs go back to their owners.
A paused buff only comes back when its owner logs in, so anyone who stays offline the whole time
loses theirs. Then remove the module, rebuild, and run
[data/sql/uninstall/mod_forever_world_buffs_characters.sql](data/sql/uninstall/mod_forever_world_buffs_characters.sql)
on the characters database.

## Limits

- Upper Blackrock Spire shares its map with Lower Blackrock Spire and counts as a dungeon, not a
  raid, so world buffs work there unless `IncludeDungeons` is on.
- The buff comes back as if you cast it on yourself, so its strength is worked out for your level
  now. A buff the game turns off above level 63 (Songflower Serenade, Rallying Cry) stays off.
- Buffs are only checked when you arrive. One you pick up while already inside isn't paused. With
  `IncludeDungeons` on, that means the Dire Maul North tribute buffs work for the rest of that run.
- If the server crashes in the few minutes between you leaving a raid and your character's next
  save, the buffs that came back can be lost.
- Entering an arena strips world buffs, the same as it always does, including ones that just came
  back from a pause.

## Credits

Author: [buildthehomelab](https://github.com/buildthehomelab)

The design follows the WoW Forever ruleset. The code is original.

## License

MIT. See [LICENSE](LICENSE).
