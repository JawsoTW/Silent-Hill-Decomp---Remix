# Randomizer mode (`remix`, `remix_items`, `remix_keys`, `remix_starter_weapons`, `remix_weapons`)

The randomizer was called **Remix** until 20 October 2026, and its code still is:
`addons/remix`, `pc_remix*.c`, `Pc_Remix_*`, the `remix_*` keys. What you see says
Randomizer: the PC Settings page, the launcher (`SilentHillPC_Randomizer.exe`),
the add-on (`SilentHillPC_RandomizerAddon.dll`), its settings file
(`randomizer.cfg`, made from a `remix.cfg` once if there is one) and the log
(`[RANDOMIZER]`). The port's earlier randomizer -- the police-station roguelike
with its Lua scripting, `randomizer = 1` in `config.cfg` -- was removed with it.

Three independent modes that share one seed:

- **Randomizer rooms** (`remix = 1`): a different building every seed. Interiors
  (both schools, the hospital in both worlds, Nowhere) are rebuilt as new layouts.
  Each door leads somewhere it didn't in the original, and every room stays
  reachable. In the schools and the Otherworld hospital a door may lead to a
  room on another floor (see [Rooms across floors](#rooms-across-floors)).
- **Randomizer items** (`remix_items = 1`): the common pickups (health drinks, first aid
  kits, ampoules, ammo) move to new spots, spread across different rooms, and each
  is of a random type of its kind: a health spot holds a Health Drink, First Aid
  Kit or Ampoule, an ammo spot handgun bullets, shotgun shells or rifle shells,
  weighted by how often the game itself places each (see [Pickup types](#pickup-types)).
  This also covers maps with no rewirable doors: Central and Old Silent Hill's
  streets, the resort and the sewers.
- **Randomizer keys** (`remix_keys = 1`): key items and other world items that are
  plain pickups move to another spot in their own part of the building, so each
  is reachable exactly when the original was. Spots are other items' own
  places, and whatever lands there is drawn the way the original item was. Items whose model is part of their room's geometry (the
  school's Chemical, Rubber Ball and Picture Card among them) are carried by a
  copy of that model, so they can leave their room too. That's 23 items across
  both schools, the hospital and Nowhere. The cafe's kitchen knife, flashlight
  and pocket radio stay where they are. A moved key rests at the height of the
  item that was at its new spot. In Old Silent Hill the Key of
  Lion, Key of Woodman and Key of Scarecrow each move to another item spot you
  can reach before the door in the Levin St house that they open (one of the
  others' spots, or a health or ammo pickup's). The steel pipe does the same
  (it trades like the three keys: its own spot in the sketchbook's lane takes a
  pickup or one of the keys). The bonus items (Katana, Channeling Stone,
  Chainsaw, Rock Drill) and the Gasoline Tank never move with the key items; they are
  `remix_unlockables`'. Pickups with their own animation
  (the medallions, cutscene keys) stay put.

- **Between areas** (no toggle of its own: always on with `remix_keys`,
  `remix_weapons` or `remix_unlockables`, each for its own items): a few items
  trade places with an item of another area. The Keys of Lion, Woodman and
  Scarecrow may each turn up in the convenience store (in place of one of its
  health pickups). Whatever leaves
  an area is replaced there by what came from the other one, so nothing is lost
  and nothing doubles. The other area is never loaded: the arriving key's model
  is read straight from its own area's files into memory, copied, and drawn
  from that copy. See [Between areas](#between-areas).

- **Randomizer starter weapons** (`remix_starter_weapons = 1`): a random starting
  gun (Handgun, Shotgun or Hunting Rifle, fixed by the seed; once the save has
  the UFO ending, the Hyper Blaster one seed in four) in place of Cybil's
  Handgun, with that gun's ammo. The Handgun then lies somewhere in the world.
- **Randomizer weapons** (`remix_weapons = 1`): every weapon found in the world at a
  random health/ammo spot anywhere in the game, or on a key item's spot when
  that key can safely take the weapon's place. The two are independent; see
  [Weapons](#weapons).
- **Randomizer unlockables** (`remix_unlockables = 1`): the next-fear bonus items --
  Katana, Channeling Stone, Chainsaw, Rock Drill -- each at a random health/ammo
  spot anywhere in the game (the Channeling Stone always before the fog
  school). They still show, and can be taken, only once unlocked. Once the save
  has the UFO ending, the Hyper Blaster lies at one too (with this or
  `remix_weapons`) instead of being handed to you. The Gasoline Tank is taken out of the game (its spot may hold a
  weapon or an unlockable) and the Chainsaw and Rock Drill need no fuel,
  wherever they lie, only Next Fear (when the tank would have appeared) --
  unless both are kept home, when the tank and the fuel
  rule are as in the original. Off, everything stays home as in the original.
  See [Unlockables](#unlockables).
- **Kept items** (`remix_fixed_items`): a list of items that stay at their
  normal spots even with their toggle on; the rest move around them as they
  would anyway, and nothing lands on their spots (the Randomizer launcher's **Item
  List** tab).

- **Randomizer enemies** (`remix_enemies = 1`): each regular monster spawn becomes
  a random pick for its kind of area, kept for the seed.
  - Indoors (every map with `MapFlag_Interior`: both schools, both hospitals):
    grey children, mumblers, creepers, groaners, nurses and doctors. No rompers
    or hanged scratchers indoors; larval stalkers too in the normal school
    (`SC` maps). In the Otherworld school and hospital
    (`SU`/`HU` maps), wormheads instead of groaners.
  - Nowhere (`map7_*`): stalkers, creepers, wormheads, nurses and doctors. No
    grey children, mumblers or groaners.
  - The streets (the town, central Silent Hill, the resort): air screamers,
    groaners, rompers and creepers.
  - The Otherworld streets: night flutters, wormheads, rompers and creepers.
  - The sewers: creepers, hanged scratchers, groaners and rompers; the
    Otherworld sewer: creepers, hanged scratchers, mumblers and wormheads.
    The sewers' own hanged scratchers always stay hanged scratchers.
  - The amusement park: grey children, mumblers, rompers, creepers, wormheads,
    night flutters and larval stalkers.
  - Scripted, ambush or tougher variants and boss spawns (the park's Cybil
    fight among them) stay vanilla everywhere.

Use any combination. Story progression is untouched, so a full playthrough works.
With both off, nothing in this document is active and the game is unchanged.

## Installing (the add-on)

The randomizer is an add-on, not part of the game exe: **SilentHillPC_RandomizerAddon.dll**
(`pc_port/addons/remix`, built by the normal CMake build next to
`SilentHillPC.exe`; turn it off with `-DSH_BUILD_REMIX_ADDON=OFF`). Put the DLL
beside `SilentHillPC.exe` and the game loads it at start-up
(`[RANDOMIZER] add-on loaded` in `SilentHill.log`); remove it and the game is the
base game again, PC Settings without the Randomizer page included. The exe keeps
only `pc_remix_host.c`, a forwarder per engine hook that does the vanilla thing
when no add-on is loaded (see [Hooks](#hooks)). A DLL built for another game
build is refused (`REMIX_ADDON_API` in `pc_remix_addon.h`). The static-map build
(`SH_STATIC_MAPS`, iOS) has no exports for it to link against, so it has no
randomizer.

Its settings live in their own file, **randomizer.cfg**, next to `config.cfg`, which
keeps none of them. The first start with the add-on writes `randomizer.cfg` with every
key documented, taking the settings of a `remix.cfg` (the file's name before the
rename) if there is one, or else any `remix_*` keys an older build left in
`config.cfg`. The old `remix.cfg` is left where it is.

```
remix       = 1         # rooms
remix_items = 1         # items
remix_keys  = 1         # key items
remix_starter_weapons = 1 # random starting gun
remix_weapons = 1       # weapons anywhere in the game
remix_unlockables = 1   # bonus items anywhere in the game; Chainsaw/Rock Drill need no fuel, only Next Fear
remix_fixed_items =     # optional: items that stay home, e.g. KeyOfLion,Katana
remix_seed  = 0         # same seed -> same layout and item spots (0..99999999)
remix_maps  =           # optional: e.g. map1_s00,map1_s01 (empty = all supported)
```

In game, **Options → PC Options → Randomizer** (the page after Controls, which the
add-on adds through `pc_options_addon.h`) has a row
for every mode, each Off/On: **Randomize Rooms**, **Randomize Items**, **Randomize Keys**,
**Randomize Enemies**, **Random Start Gun** (`remix_starter_weapons`),
**Randomize Weapons** and **Randomize Extras** (`remix_unlockables`), then
**Seed_(8_digits)**. On the seed row, left/right or a click rolls a new random
seed of up to 8 digits (1 to 99999999). Seeds top out at 8 digits everywhere: a
bigger `remix_seed` in `randomizer.cfg` is cut to 99999999, and the Randomizer launcher's
seed box stops there. Every seed up to that fits in 32 bits, so a seed keeps the
layout it built before. All are saved to `randomizer.cfg` and take effect the next time an area loads;
no restart. The starting gun is the exception: it's handed out once, in the
cafe, so set it before a new game. Rolling a new seed mid-game is safe: the next
area you enter is rebuilt from it (the starting gun you already have stays).

The randomizer has a launcher of its own, kept apart from the main one:
**SilentHillPC_Randomizer.exe** (`pc_port/launcher/SilentHillPC_Remix`, built with
`pc_port/build_randomizer_launcher.bat`, which copies it into `pc_port/build` next to
`SilentHillPC.exe`). It edits `randomizer.cfg` (through the main launcher's
`ConfigManager`, which writes back only the keys it changed) and warns when the
add-on DLL is not in the folder. It has the
same modes, the seed (up to 8 digits, with a **Random** button) and `remix_maps` (only these
maps); **Save & Play** saves and starts the game. Its **Item List** tab has a
tick per weapon, key item and unlockable, in three sections; an unticked one is
written to `remix_fixed_items`. The main launcher has no randomizer settings.

## What changes and what doesn't

| | |
|---|---|
| Ungated room doors | Rooms mode: rewired into a new, fully connected layout. Each room keeps its number of doors. |
| Key / locked / story-flag / scripted doors | Unchanged: same destination, same requirements. |
| Doors between maps (`SysState_LoadOverlay`) | Unchanged. |
| Common pickups (health, ammo) | Items mode: trade places with each other, drawn in the pose of the item they replaced. |
| Plain key items | Keys mode: moved within their zone (see below). |
| Scripted key items, puzzles, cutscenes, monsters | Unchanged. |
| Paper map | With rooms on, still shows the original building, so it's no longer a reliable guide. |
| Saves | Layouts are a pure function of `(remix_seed, map)`, so a save reloads into the same building. Changing the seed mid-game changes the building (still safe; see below). |

Covered maps (regenerate to change; "—" means that mode has nothing to do there):

| Map | Area | Rooms: free doors | Items: pickups / spots |
|---|---|---|---|
| map1_s00 | Midwich School (fog) | 30 | 4 / 11 |
| map1_s01 | Midwich School (fog) | 42 | 5 / 14 |
| map1_s02 | Midwich School (otherworld) | 38 | 9 / 18 |
| map1_s03 | Midwich School (otherworld) | 30 | 9 / 11 |
| map2_s00 | Old Silent Hill (fog) | — | 20 / 34 |
| map2_s02 | Central Silent Hill (fog) | — | 5 / 19 |
| map3_s01 | Alchemilla Hospital (fog) | 24 | — |
| map3_s03 | Alchemilla Hospital (otherworld) | 34 | 5 / 11 |
| map3_s04 | Alchemilla Hospital (otherworld) | 12 | 2 / 4 |
| map3_s05 | Alchemilla Hospital (otherworld) | 14 | 4 / 6 |
| map3_s06 | Alchemilla Hospital (fog) | 10 | — |
| map4_s03 | (no paper-map tag) | — | 2 / 3 |
| map5_s01 | Resort area | — | 4 / 19 |
| map6_s00 | Resort area / sewers | — | 2 / 27 |
| map6_s03 | Sewers (otherworld) | — | 2 / 20 |
| map7_s01 | Nowhere | 24 | 8 / 8 |
| map7_s02 | Nowhere | 40 | 7 / 7 |

Where a map has as many spots as pickups (Nowhere), items only swap places with
each other.

## How it works

### Interiors are room graphs

An interior is not a continuous building. Its rooms are islands scattered across
the map plane, 25–135 units apart, joined only by `SysState_LoadRoom` event rows.
Each door has a **twin** on the far side, and the door's arrival point is exactly
the twin's doorway. In map1_s01 all 48 room doors pair up this way with zero error.

For a door X, the *landing* (where you appear when you come **into X's room
through X**) is therefore the twin's `(eventParam, mapIdx, field_8_24)`:

- `eventParam`: the arrival mapPoint (it carries its own facing and loading-screen data);
- `mapIdx`: on a room transition this is the BGM track to switch to;
- `field_8_24`: whether the arrival is on a camera rail.

All three belong to the *destination*. Joining door A to door C means A takes C's
landing and C takes A's. No coordinates are invented: every arrival is one the
designers authored for that exact doorway.

### Rooms, zones and why progression can't break

`pc_port/tools/gen_remix_data.py` harvests, per map:

- **Free doors.** A door is free only if it is ungated: no `requiredEventFlag`, no
  `requiredItemId`, not Item-activated, it is the only row on its doorway (a
  sibling row means a locked or cutscene state), and it has exactly one mutual
  twin that is also free. `disabledEventFlag` values ≥ 867 are paper-map marks
  that `Event_Update` ignores for gating, so those don't count as gates. Every
  other door is **fixed**.
- **Rooms.** One node per camera-switch volume (`VC_ROAD_DATA.lim_sw`). A doorway
  belongs to the box it sits in, and a door is only free if its landing lands in
  that same box. Boxes are deliberately **never merged**: a real room spanning
  several boxes just becomes several nodes. That's always safe, because a
  connected node graph stays connected when nodes are glued back together.
  Merging boxes that only touch across a wall could hide a real disconnection.
- **Zones.** Connected components of rooms over free door pairs.

At runtime `RemixLayout_Doors` (pure C, `pc_remix_layout.c`) rebuilds each zone
independently:

1. Grow a random spanning tree room by room. Each newly attached room spends one
   of its doors on a door already in the tree, and multi-door rooms are preferred
   while only one tree door is open, so the tree can't run dry.
2. Pair the leftover doors at random, never joining a room to itself and avoiding
   repeated room pairs where possible.

Guarantees, for every seed: the pairing is symmetric, no door leads back into its
own room, every room keeps its door count, every zone stays connected, and no pair
crosses zones. Fixed doors keep their vanilla destinations, so the zone-to-zone
structure (everything keys, story flags and locked doors gate) is exactly vanilla.
Under any flag/item state the set of reachable rooms is the same as in the
original game. A zone that can't be rebuilt in 64 attempts keeps its vanilla wiring.

### Rooms across floors

A building's floors are separate maps (the fog school's two floors, the
Otherworld school's, the Otherworld hospital's three) joined by stairs. Where
the stairs are always open both ways, the zones they join are one **door
group** (`door_groups` in the generator, `REMIX_DOOR_GROUPS`). The rooms joined
to a staircase are found by walking and through fixed doors open both ways,
never through a rewirable door: that door may lead elsewhere in the new layout,
so a room behind it is not really joined to the stairs. (An earlier generator
walked through them, which made the fog school lobby's one-door box count as
joined to the stairs; its door could then lead into a bathroom and back, a dead
end, in about 1 seed in 20.)

| Group | Zones |
|---|---|
| Midwich School (fog) | map1_s00 zones 0, 2, 3; map1_s01 zone 0 |
| Midwich School (Otherworld), first | map1_s02 zone 1; map1_s03 zones 2-4 |
| Midwich School (Otherworld), second | map1_s02 zone 2; map1_s03 zones 0-1 |
| Alchemilla Hospital (Otherworld) | map3_s03 zones 0, 2; map3_s04 zone 0 |

A group's free doors are rewired together (`RemixLayout_DoorGroup`) by the same
spanning-tree build as one zone, with the rooms the stairs join counted as one
node: they're always joined, so the build only has to connect the nodes. Every
room of the group stays reachable from every other, as it was, so nothing that
was reachable stops being reachable. About half the doors of a group end up
leading to another floor.

Every map of a group works the same answer out from the seed alone, so a door
on one floor and its partner on the other agree without either being loaded. A
door that leads to another floor becomes a `SysState_LoadOverlay` row (the same
game function as a room door: it reads the target map, the arrival point and the
camera rail from the row). The arrival point is the other map's own vanilla
arrival for that doorway (`REMIX_DOOR_ARRIVALS`), copied into this map's point
table, because a map load reads it from the map it leaves.

The stairs themselves, every gated door and every scripted door keep their
vanilla destinations.

### Pickups

Each common pickup is an `EventCallback` row (`TouchFacing`, its
`disabledEventFlag` latches when taken) plus a model the map's
`updateWorldObjects` draws from `g_CommonWorldObjects[]` behind
`if (!Savegame_EventFlagGet(flag))`, gated on the chunk the player is in. The
generator ties the two together through that flag.

At runtime the pickup's trigger mapPoint is moved in our copy of `mapPoints`.
Because the map's own draw is gated on the *original* chunk, the mode wraps
`updateWorldObjects`. For the length of the map's call, every untaken moved pickup
reads as taken, so the map skips it. The flags are restored immediately after, and
the mode draws each pickup at its new spot (within 24 units, only where
`func_800808AC` reports floor, at `Collision_SurfaceGet` ground height).
`WorldGfx_ObjectAdd` copies the pose by value, so nothing points at temporary
state.

Pickup spots are exactly the map's own pickup places: spot *i* is pickup *i*'s
vanilla place, so pickups only trade places with each other and nothing lands on
bare floor. A moved pickup is drawn with the authored pose (`s_Pose`, height and
rotation) of the item that was at its new spot, found through
`MapOverlay_GetSymbol` from the symbol the generator read off the map's draw
call. Pickups are spread across different rooms first.

### Pickup types

With `remix_items`, every common pickup's type is picked from (seed, its flag):
health stays health and ammo stays ammo, weighted by how many of each the
covered maps place in vanilla:

| Health | weight | Ammo | weight |
|---|---|---|---|
| Health Drink | 46 | Handgun bullets | 48 |
| First Aid Kit | 18 | Shotgun shells | 14 |
| Ampoule | 3 | Rifle shells | 10 |

So about 69% / 27% / 4% of health spots and 67% / 19% / 14% of ammo spots; these
rounded percentages are the defaults of `remix_weight_health_drink`,
`_first_aid`, `_ampoule`, `_handgun_ammo`, `_shotgun_ammo`, `_rifle_ammo`
(`g_RemixCfg.remixWeights`, by `CommonPickupItemId`). The launcher's Item types
tab shows them as two groups of linked sliders, each always adding up to 100%; a
kind whose three are all 0 keeps its vanilla types. The
pick depends only on the flag, so a pickup is the same type on its own spot, a
moved spot or in another area (Nowhere's two maps share flags and agree).
`Event_CommonItemTake` asks `Pc_Remix_CommonType` for the type it hands out;
the draw uses the same pick. A pickup left on its own spot but given another
type is drawn by the mode with the map's own pose, like a moved one. Ammo counts
are the game's own per type (and per difficulty).

### Keys

The generator finds key items picked up through a plain `Event_ItemTake` of a
non-common item. Their model must be drawn by the map as
`if (!Savegame_EventFlagGet(flag)) WorldGfx_ObjectAdd(&<sym>.object, &<sym>.position, ...)`,
with `<sym>` an `s_WorldObjectPose` or `s_WorldObjectPlacement`. The only other
rows allowed on the key's spot are "already taken" messages gated on the key's
own flag.

**Reachability.** A key only moves to a spot in its own *zone*: another key's
or a common pickup's original spot (its pickup point, where the item is seen).
A key model is mostly room geometry whose mesh sits metres away from its pose
origin (the hospital's Plate of Cat is posed at (-20, -19.2) and seen at
(-16.8, -20.8)), so a moved key is drawn with its own pose shifted by however
far the new spot is from its own: the mesh lands on the spot, at its own height.
It also picks up the way the item that was there did: that row's trigger type
and its trigger line (`triggerParam0/1` of the map point), so it can be reached
from wherever the original could; on an enemy spawn spot, which was nobody's
place, it uses the plain facing trigger. A common pickup moved onto a key's spot
takes that key's trigger the same way and is drawn on the floor at the spot.
Every room in a zone can be reached
as soon as any one of them can, through ungated doors. So the key is reachable
exactly when the original was. It can never land behind the door it opens, and
no story gate is skipped. A key's camera box often has no door of its own, so
its zone is inferred from touching camera boxes. Every box holding any door is
labeled first: a free door's box with its zone, and every gated, scripted or
map-exit door's box with a unique label. A group of touching boxes only takes a
zone if all its labels agree. A room entered through a locked door therefore
can't borrow a neighbor's zone across a wall: its own door box carries a
conflicting label, and the whole group is skipped. A group whose only label is
one gated door is a zone of its own (ids from 128): a room behind a single
locked door, all of which opens the moment that door does. This also lets keys
move on maps with no remixable doors at all.

**Optional bonus items** (`OPTIONAL_ITEMS` in the generator: Katana, Channeling
Stone; the Rock Drill and Chainsaw too, but they are in `STAY_PUT_ITEMS`) are
marked `anywhere`. Nothing in the story needs
them, so missing one can never block progress, and they may go to any original
item spot on their map, zone or not (`REMIX_NO_ZONE` slots, which nothing else
uses).
The Katana's room only opens once it's unlocked (`EventFlag_57`, set by the
bonus extras on a next-fear start). Until then it is *pinned* (`UNLOCK_FLAGS`
in the generator, `unlockFlag`/`pinned` in `s_RemixKey`): it stays where it
is, and the layout puts nothing else on its spot, so no item ends up in a room
you can't enter. Their pickup rows keep their `requiredEventFlag`, so a moved one is
drawn and can be picked up only once it is unlocked, as in vanilla. Every
moved item's pickup rows get map points of their own (appended past the map's
`MAP_POINTS` in the copied header), so only the pickup travels: an "already
taken" or "not unlocked yet" message on the same vanilla point stays where it
was.

**Street keys trade places.** A key the story needs on a street map whose spot
is in no zone (the Key of Lion, Key of Woodman and Key of Scarecrow in Old
Silent Hill) moves to a spot the player can reach before the place the key is
used: another street key's own spot, or a common pickup's
(`STREET_SWAP_SPOTS` in the generator, `swapOk` on the slot). In Old Silent
Hill the keys open the door in the Levin St house; before it, the town is the
block the first-visit roadblocks close off, plus the alley outside the cafe, the
lanes behind it (the Key of Woodman's, and the steel pipe's) and the house's own
rooms up to that door. That gives twelve pickups (the Key of Lion's corner of
Finney St, the steel pipe and sketchbook's lane, four in the house, beside the
Key of Scarecrow on Ellroy St, between Bachman Rd and Ellroy St and between
Levin St and Bachman Rd north of the roadblocks, two on Bachman Rd by the cafe,
and the Key of Woodman's lane) and the three keys' own spots. Each street key goes to a different one of those spots, its own
included (so it may stay); a pickup whose spot a key took goes to a street
key's spot that no key took
(there are always exactly as many). A key bound for another key's spot only
goes once that key has left it (the same frame is fine, so two keys can trade).
Bonus items going "anywhere" never take these spots. The Key of Woodman's
pickup is a scripted crouch-and-take, not a plain `Event_ItemTake`; its only
state is the item and its flag, so it plays the same wherever the key lies
(`EXTRA_KEY_PICKUPS` in the generator). It's fixed by
the seed alone, from the moment the map loads, so it never moves while you
watch and is the same after any reload. The open street of a town map is
reachable once you are on that map; story gates there are doors (rooms behind
them are zones of their own and never offer a trade). That is a judgment about
the level design, not something the data proves, so it is only used outdoors:
a no-zone key indoors (Nowhere's Amulet of Solomon) stays put. A key's fallback
is its own spot, which then may also hold a pickup or another key.

**Keys trading spots.** Keys in one zone (or optional items on one map) that
have nowhere free to go trade their own spots in a random rotation, so a room
whose only item spots are its keys' still gets shuffled. Every spot
taken is one its owner left, so placed keys never share a spot; only a fallback
home can meet the key that took that home.

**Never in view.** A key changes spots only while the screen is fading or
black, or while the player is more than 24 units from both its old and new
spot. Otherwise it waits at its own spot (state `WAITMOVE`). An area is
entered with a fade from black, which is when a key in the starting room moves.

**Heights.** A moved key rests on the surface the item at its new spot rested
on: the bottom of that key's or pickup's model as the map draws it (pose height
plus the model's lowest vertex), or the floor on an enemy spawn spot; the key's
own lowest vertex goes there. A key's model is read from its own copy, or from
whichever loaded set holds it right then: the global and item model sets, or a
room chunk that is fully in (read at once and not kept, so the chunk leaving
later doesn't matter). That covers the hospital plates and the Disinfecting
Alcohol, whose models live in their rooms: without it, what took a plate's spot
lay on the floor inside the furniture (the Intensive Care Unit, the Director's
desk) and the alcohol floated at its shelf's height. A key whose model can't be
read yet keeps its height above its own spot's surface until it can. The Hammer
and the Axe hang on walls: what takes their spot lies on the floor below. A
common pickup moved onto a key's spot rests on that key's surface the same way.

**Items on keys' old spots.** A common pickup may also go where a key lay that
moved away: those spots join the pickup spots, each counted as a
room of its own so they spread. Such a pickup rests on that key's surface and
picks up the way that key did. A spot a key is going to is never used, and a
pickup whose own place a key is taking always gets another spot (a free one,
else one a pickup with a free home of its own was given, and that one stays
home), so no two items ever lie on one spot. With items off the same pass runs
with every pickup at home, so only a pickup whose place a key (or an item from
another area) takes moves, to a free spot. Two keys can share one home (the
Nowhere pliers and screwdriver), so a key moving doesn't always free a spot; if
the pickups can't all get a spot of their own, a key that took a pickup's spot
stays home instead (never one whose home another key is bound for), and the
spots are worked out again. A key at its own home keeps its own vanilla trigger
point, so keys sharing a home still lie apart as in vanilla, and a key that
isn't moving stays home rather than taking its fallback spot. Two pickups the
map lays side by side (half a metre apart) are separate spots.

This is checked on every remixed map with its real event table: 23 maps, 2000
seeds each, with items + keys, items + keys later in the game (bonus rooms
unlocked, Next Fear), items only and keys only, between areas on -- no two
pickups end on one spot that weren't on one in vanilla.

**Pickups on the open street.** A pickup outside every camera volume (much of
Old Silent Hill's streets) still lies where the game itself puts it, so it is a
spot the player can reach: all 32 of Old Silent Hill's health and ammo pickups
take part (it was 20). A key's fallback spot isn't held for it: if a key ever falls back to it,
it may share it with an item.

**Cafe items stay.** The cafe's kitchen knife, flashlight and pocket radio never
move (`STAY_PUT_ITEMS` in the generator), not even with `remix_weapons`: the
radio cutscene that unlocks the cafe's door waits for the knife's and the map's
pickup flags (`EventFlag_M0S01_PickedUpKnifeAndMap`).

These keys may be scripted beyond the plain pickup, as long as the extra script
only reads and writes event flags (the street keys also mark the paper map) or
switches the item itself on or off (the radio and flashlight), so where the
pickup happens changes nothing else. Other rows on a key's spot may be any
messages; they stay at the vanilla spot. Their models may also be array
elements (`&g_WorldObject4[2].object`); the table carries the index.

**Visibility.** A model in the map's global LM, or in the item LM, draws
anywhere. One that lives in a geometry chunk only resolves while the player
stands in that chunk's 40×40 IPD cell. On the first frames after a map loads,
the mode checks which kind each key is (`Lm_ModelFind` on a copy of the model).
Global models get their *wide* spot (anywhere in the zone) right away.
Until that check finishes, every key stays at its vanilla spot.

**Carrying a chunk-local model.** A chunk-local model is copied out of its room
and drawn from the copy. On PC the reformatted model and mesh *headers* are
heap-allocated, but the arrays they point at (primitives, vertices, normals,
shade bytes) live in the chunk's raw IPD buffer, which is overwritten as soon as
that chunk slot is reused. So the copy owns all of them, with the same zeroed
tail the strided GTE copies over-read (`lm_validate.c`, rule V9). Each
primitive already carries its texture page and CLUT; the texture pool slots
behind them get one extra `refCount` each, so the pool can't hand them to
another TIM while the copy lives. The copy is drawn with `lmIdx` 2 (the global
set's index), which the draw path never re-checks against a chunk.

To copy it, the key's own cell has to be resident and textured. In an interior
that isn't resident, its cell's IPD (or a neighbour's, where the model sits
across a border) is read straight off the disc into the between-areas buffer
and the model copied out of it, as for an item from another area
(`key_copy_from_disc`). An interior streams only the two chunk slots around
the player, so waiting for the cell to come in meant a key whose room you
hadn't walked past yet stayed at its vanilla spot, and after 30 seconds gave
up on its new one: where it lay depended on the route you took. Exteriors
preload every chunk and wait for it. Meanwhile the key waits at its vanilla
spot (state `COPYING`), drawn by the map as usual.
Once copied, it moves to its wide spot, but only while the player is more than
24 units from both spots, so it never vanishes or appears in view. The pins are
dropped when the key is taken, and on the next map load. A slot that drops to
zero then is wiped, as `Ipd_TexturesRefClear` would have done without the pin.

**Hospital plates.** The four plates (Turtle, Hatter, Cat, Queen) are an
exception: their room-geometry model is only a fragment, posed for its own room,
so a copy of it hangs in the air at an angle. A moved plate (in its own area or
arrived from the other hospital area) is drawn with its inventory model instead,
`ITEM/UNQ67.TMD`-`UNQ6A.TMD`, read off the disc once and kept. Its texture,
`ITEM/TIM03.TIM`, is already in VRAM in every hospital area for the inventory
(`InvItemLoadFlag_Tex3`); without it the plate falls back to the copy. The model
is laid flat (its thinnest axis made vertical, the side its faces are drawn from
on top), scaled to 0.25 m along its longest side, centred on its spot and
resting on the surface the spot's item lay on. `Pc_Remix_WorldDraw`, called from
`Gfx_WorldObjectsDraw`, sorts it into the world's ordering table at the world's
depth scale with its true per-vertex depth, so it sorts with the room. The TMD
drawers normally take their bucket from the depth cue tuned for the item camera,
which at room distances puts every face in the farthest bucket, under the floor
drawn after it; `g_PcTmdWorldOtz` (libgs_stub.c) gives them the world's bucket
(deepest raw SZ >> 3) and their lit faces a flat colour, as the GTE lights then
belong to the world. At home the map draws it as usual.

Copies need the expanded texture pool (`resident_textures=1`, the default),
whose virtual slots are safe to pin. Interiors texture every loaded chunk
wherever the player is. Exteriors only texture the chunks around the player,
since that is their draw distance, so there the copy claims its far chunk's
materials itself (`Ipd_MaterialsLoad` + `Lm_MaterialFlagsApply`), copies the
model, and lets the chunk's own claims go again at once. The chunk is back to
untextured and is not drawn through the fog; the copy keeps its pins, and the
item moves once those TIMs have loaded. Without the pool, if the model can't be
found in its room, or if the copy hasn't happened after about 1800 frames, the
key takes its *cell* spot instead (same zone and same IPD cell, usually the
vanilla spot).

**The item set and the first frames.** `BG_ITEM.PLM` is read straight into
`g_WorldGfxWork.itemLmHdr`, and it only becomes a valid 64-bit header once
`LmHeader_FixOffsets_PC` runs on it, a frame or more later. Until then its
model-list pointer is raw PSX bytes. Reading it crashed the game on entering a
school area with chunk-local key models, whose classification fell through to
the item set. The mode now waits for `itemLmHdr.isLoaded` (the engine's own
check) before classifying with the item set. It also makes none of its own draws
until then, because `WorldGfx_ObjectAdd` falls back to the item set for models
it can't find elsewhere. Moved pickups and keys simply stay hidden for those
first frames.

**Fallback ladder** (wide → cell → vanilla spot), each rung strictly safer. A
copied model keeps drawing from its copy on every rung.
- if the mode draws a moved key and nothing resolves (`lmIdx` stays 0 with room
  left in the object list), the model can't draw there;
- if the spot's collision is resident and `func_800808AC` finds no floor within
  10 units of the player, the spot is unreachable.

`RemixLayout_Keys` keeps all fallback spots distinct, and keeps every wide spot
off any other key's spots, so no combination of fallbacks can stack two keys.
Common pickups never use a spot a key is going to or staying on. With items
off, a common pickup on such a spot moves to a free spot (see "Items on keys'
old spots").

The original draw of a moved key is suppressed by a `WorldGfx_ObjectAdd` hook,
not by faking its flag. The hook matches the model and `&<sym>.position`
pointers, and is active only during the map's own `updateWorldObjects`. Key
flags may drive story logic in that same update, so they're never touched. The
key models are found by name: `MapOverlay_GetSymbol` checks the map DLL first,
then the exe's exported shared storage (`data_stubs.c`), the same order the
map's own references resolve in.

### Between areas

`REMIX_X_RULES` (from `XMAP_RULES` in the generator) pairs a list of items in
one area with a list of item spots in another, with a chance. Today:

| Leaves | For | Chance |
|---|---|---|
| Key of Lion, Woodman, Scarecrow (`map2_s00`) | one of the 4 health pickups in the convenience store (`map0_s02`) | 1 in 3 each |
| Chemical (school 2F, `map1_s01`) | one of the 4 health/ammo pickups on the school's first floor (`map1_s00`) | 4 in 9 |
| Shotgun (Otherworld school 1F, `map1_s02`) | a pickup on its second floor (`map1_s03`) reachable at that point (6 spots from its bathroom) | each spot as likely as any of its own floor's |
| Plates of Hatter, Cat, Turtle, Blood Pack, Lighter (Otherworld hospital, `map3_s03`) | a pickup on the floors joined to theirs by the stairs (`map3_s04`, `map3_s05`) | likewise |
| Plate of Queen (`map3_s04`), Hammer (`map3_s05`) | a pickup on the other hospital floors | likewise |

The Rubber Ball and Picture Card stay on the first floor: the second floor is
first reached through the Picture Card's door (the girls' toilets warp you up).
The east stairwell's own door opens only after the Teacher's Room phone scene,
and its two sides lie 0.05 apart inside one camera box, so the reachability
graph splits that stairwell off by hand (`ISLAND_SPLITS` in the generator).

The rows below the first two come from reachability, not a hand-written list
(see "Keys past their zone"): a key may lie on any item spot of the building
that is reachable, and back, while it lies on its own spot. Every such spot --
on its own floor or another -- is equally likely, and a key may stay home.

Both areas work the trades out from the seed alone (`xmap_plan`), so each one
knows what left it and what arrived without the other being loaded, and a save
reloads into the same trades. A trade is one-for-one: the item that left an area
is replaced, at its own spot, by the item it traded with.

In the area an item **leaves**:
- its pickup rows point at a map point far outside the world, so they can never
  fire (`TriggerType_None` can't be used for that: it always fires);
- it isn't drawn (a key through the `WorldGfx_ObjectAdd` filter, a common pickup
  by holding its flag set for the length of the map's own draw);
- its spot is blocked for this area's own remix (`g_RemixLayoutBlocked`, and the
  items' spot list), so nothing else lands on it.

**Remarks behind pickups.** The game answers the first event row that
matches, and a spot's remark ("Nothing useful in the boxes.", or "Nothing
unusual." once its own item is taken) is often listed before the pickup row of
an item that now lies there; in vanilla the item that lay there came first. So
any remark within a unit of a moved key, a moved pickup or an arrival, and
listed before its row, moves to the end of the table with its conditions
unchanged (its old row points far away): it answers once nothing else there
does (`messages_behind_pickups`). Where the table is full (Nowhere), the remark
waits for that item's flag instead.

In the area an item **arrives**:
- it gets a pickup row of its own, appended to the event table: a copy of the
  row of the item that was at its spot (so it is picked up the same way, from
  the same distance), with that item's flags swapped for the arrival's own flag
  and required flag. The row calls `Pc_Remix_XTake` through a copy of the map's
  event-function table, which calls `Event_ItemTake` (keys, with the take message
  appended to a copy of the map's message table) or `Event_CommonItemTake`
  (health, ammo), the same calls the item's own area makes. The flag set is the
  item's own, so once taken it is gone from both areas.
- a common pickup is drawn from the item model set every area has, on the
  surface of the item it replaced;
- a key's model only exists in its own area's files. The IPD of the cell its pose
  lies in (then the 8 cells around it, then the area's global model set) is read
  into one reusable buffer, reformatted, and the one model is copied out
  (`copy_build`, the same copy the in-area key moves use). Its textures are
  claimed into this area's texture pool by name and pinned, so this needs
  `resident_textures = 1`; without it only common pickups would trade, and as
  both current rules hold a key, nothing trades. The file is read when the item
  first comes within draw distance, never at load. Nothing of the other area is
  drawn or kept resident.

### Keys past their zone

A zone (rooms joined by ungated doors) is the safe minimum: anything in a key's
zone is reachable whenever the key is. In the schools, the Otherworld hospital
and Nowhere (`REACH_MAPS`), the generator proves more, from the event tables of
the whole game (`reach_set`):

- A **room** is an island of camera boxes. Boxes of one room lie at most ~4
  units apart, rooms 11+ (doors between them are teleports).
- Every `SysState_LoadRoom` and `SysState_LoadOverlay` row is an **edge** from
  the room of its doorway to the room of its arrival, labelled with what it
  takes: its required flag, its required item, a gating disabled flag. A row
  that fires on its own (`TriggerType_None`) or shares its doorway with other
  rows (a "locked" message may answer first) is labelled as unknown and never
  counts as open. A `LoadOverlay` row's arrival point is one of its own map's
  points in the target map's coordinates (that is how the game reads it).
- **Entries** are every arrival point of a row from another map, and every
  arrival of an automatic row (where a cutscene puts you: we can't tell what led
  there). More entries only ever make the sets smaller.
- For a key's room R: every flag that every route from an entry to R needs is
  set by the time the key is picked up (flags only get set). With exactly those
  doors open, the rooms R can walk to **and back from** are reachable while the
  key lies there. The key may go to any item spot in them: in its own map
  through `s_RemixKey.reach` (a bitmask of `keySlots`, on top of its zone), in
  another map of the same building (`XMAP_GROUPS`) as a between-areas trade with
  a health or ammo pickup there.

Where the tables don't show what the game does, the generator is told by hand,
always toward fewer spots:

- `ISLAND_SPLITS`: the Otherworld school's east stairwell is a node of its own
  (its door opens after the phone scene; see "Between areas").
- `CLOSED_ROWS`: the Otherworld hospital's 1F/basement stairs count as shut. The
  basement is first reached by the elevator from 2F east, past the four-plate
  door, so nothing from 3F, 2F west or 1F goes down there.
- A key whose use is walking into a room with it (`TouchAabb` item rows: the
  Blood Pack, dropped for the tentacles of room 204) never lies in that room.

The resulting spots were checked over 1500 seeds against the game's own order:
the hospital plates, Blood Pack and Lighter only land on 3F, 1F and 2F west
spots (never past the Nurse Center's plate door, never in the basement); in
Nowhere the Pliers and Screwdriver are on hand before the faucet and the screwed
plate, the Crest, Ring of Contract and Camera stay on 2F (before the 3F altar
and the Phaleg fridge), and the Bird Cage Key, Ankh, Disk of Ouroboros, the
lettered keys and the Stone of Time don't move.

Health and ammo pickups outside every zone become key spots in these maps too,
when a key's set reaches them. Nowhere's two maps are the same rooms at two
points of the story and share their items' flags, so its keys only move within
their map. The Otherworld school's Shotgun is a crouch-and-take pickup like the
Key of Woodman; its row needs the bathroom corpse moved first, so wherever it
lands it shows only once that's done (as any moved key whose rows all need a
flag), and it comes with its six shells there too.

### Weapons

**Starting gun** (`remix_starter_weapons`). `start_gun()` picks the Handgun,
Shotgun or Hunting Rifle from the seed (the Handgun when this is off). The cafe cutscene's "add the Handgun" (`func_80086470` /
`Event_InvItemCmd`, hooked as `Pc_Remix_RemapAddItem`) adds that gun instead,
with the rounds its own pickup comes with. A game begun past the cafe gets it in
its starting loadout (`Pc_Remix_StartLoadout`). Once the save has the UFO
ending (`hyper_unlocked`: `clearGameCount` and `GameEndingFlag_Ufo`, the save's
half of `Inventory_HyperBlasterUnlockTest`), one seed in four starts with the
Hyper Blaster instead. That is a roll of its own, so every other seed keeps the
gun it had before the unlock. The Hyper Blaster needs no ammo: the loadout keeps
the Handgun's, for the Handgun the seed then hides. A gun controller, the
test's other half, is left to the game: plugging one in mid-game must not
change the seed's gun or trades.

**Weapons in the world** (`remix_weapons`). These pickups take part: the
steel pipe (Old Silent Hill), Shotgun (police station and the Otherworld school: one pickup flag, two
places), Hammer (Otherworld hospital), Axe (Green Lion Antiques) and Hunting
Rifle. Each always leaves its own spot as a between-areas trade
(`weapons` rules in `REMIX_X_RULES`, generated from `WEAPON_ITEMS`):

- with one of the game's health/ammo pickups (one shared list of every pickup in
  every covered map), which then lies on the weapon's own spot; or
- with a key item whose `reach_set` holds the weapon's room: the key may lie
  there safely, and the weapon takes the key's spot. Every spot, pickup or key,
  is equally likely.

The weapon is drawn from its own world model (read from its area's files, as any
item between areas), and picked up through `Event_ItemTake` with its own count
and "Take it?" message. The kitchen knife never moves (`STAY_PUT_ITEMS`): it
is part of the cafe's radio cutscene. The Katana, Rock Drill and Chainsaw are
unlockables (below), not weapons here. The hospital's Plastic Bottle and Basement Key (map3_s01) stay put too: their
models (`BOT_HIDE`, `KEY_HIDE`) are ones the room only shows at home, so a copy
of either draws nothing.

**The Handgun.** It has no world pickup of its own. When the seed's starting gun
isn't the Handgun, that gun's own pickup becomes the Handgun, wherever it went
(`s_xHandgunFlag`; `Pc_Remix_RemapItemTake` rewrites the take). It's drawn from
the model Harry holds (`ITEM/HANDGUN.PLM`), textured from the part of `HERO.TIM`
that is always in VRAM -- or, as below, with its inventory model. Taking it sets that pickup's flag, so the gun it stands
in for is never found as well. With only `remix_starter_weapons` on, that one
pickup is the only weapon that moves: every other weapon stays home.

**Inventory models.** A moved Hammer or Axe (their world models hang on a wall
at home) and the Handgun lie on their side with their inventory models
(`ITEM/UNQ82.TMD`, `UNQ84.TMD`, `UNQA0.TMD`), drawn as the hospital plates are
(see Keys), scaled to about the real thing (Handgun 0.22 m, Hammer 0.35 m, Axe
0.75 m). Their textures are in `ITEM/TIM00.TIM` and `TIM07.TIM`, which every
area keeps in VRAM; the model's textured faces are checked once for the pages
they use, and one using any other page keeps the old way. Whatever takes the
Hammer's or the Axe's spot lies on the floor below it, not at its wall height.

**The Hyper Blaster.** Once the save has the UFO ending, the game hands it over
whenever the inventory opens (`item_screens_2.c`). With `remix_weapons` or
`remix_unlockables` on, it lies at one of the shared health/ammo spots instead
(the last rule in `REMIX_X_RULES`, `weapons == 3`, so its dice come after every
other rule's and no other trade changes): an item with no spot of its own
(`REMIX_X_NOWHERE`, map 255), so the pickup it displaces is simply gone. Its
pickup flag is 866, which no map uses. It's drawn with its inventory model
(`ITEM/UNQA3.TMD`, 0.30 m). `Pc_RemixHyper_Grant` (into the add-on's `Pc_Remix_HyperBlasterGrant`) keeps the game from
handing it over while the randomizer has it in the world or as the starting gun; when it
is the starting gun, its world pickup is the Handgun, as for any other starting
gun. Kept home (`HyperBlaster` in `remix_fixed_items`), or not placed (no
resident textures, the spot's map not remixed), the game hands it over as ever.

**Duplicates.** A gun you already carry (a second Shotgun pickup, Cybil's
Handgun in Next Fear) gives its ammo instead, with the game's own ammo prompt:
two of one gun would be two inventory slots.

### Unlockables

`remix_unlockables` moves the next-fear bonus items (`UNLOCKABLE_ITEMS` in the
generator): the Katana and Channeling Stone (Old Silent Hill's convenience
store) and the Chainsaw and Rock Drill (central Silent Hill). Each is pinned on its own map (`pinned`), and leaves it only by a
rule of its own (`weapons == 2` in `REMIX_X_RULES`), for one of every health/ammo
spot in the game, the shared list the weapons use; that pickup lies on the
item's spot. Off, they stay home.

The Channeling Stone only goes to spots reachable before the fog school (the
cafe, the alleys of `map0_s02`, and the Old Silent Hill street spots proven
reachable before the Levin St house; `EARLY_ITEMS`): the UFO ending needs it
used on the way. Anywhere later and that ending would be lost for the run.

Each still shows, and is taken, only once unlocked: the arrival's required flag
is its unlock (the Katana's door, flag 57; the stone, 58). The Chainsaw's and
Rock Drill's pickup rows require the fuel instead (flags 61/183, set by using
the Gasoline Tank on them, `FUEL_ITEMS`).

**Even when not unlocked** (`remix_unlockables_always = 1`, with
`remix_unlockables`; the launcher's box beside Unlockables). Every moved
unlockable lies in the world from the first playthrough: an arrival's required
flag is dropped when it is an unlockable's unlock (`xmap_req`: the Katana's 57
and the stone's 58, both set by the Next Fear extras in `func_800904F4`; the
Chainsaw's and Rock Drill's Next Fear, 62), the door in central Silent Hill to
the Katana's room opens (its `map2_s00` row to `map0_s02` loses its
`EventFlag_57` requirement, `katana_door_open`, so the locked row behind it never
fires), and
the Hyper Blaster's rule runs without the UFO ending (`hyper_available`). Taken
that way, the Hyper Blaster would be the game's locked one (no aim, "needs
unlocking"), so `Inventory_HyperBlasterFunctionalTest` (`item_screens_1.c`)
asks `Pc_RemixHyper_Works` (pc_remix_hyper.c, into the add-on's `HyperBlasterWorks`) and answers "unlocked". The starting gun still
takes a real unlock. Items kept home keep the game's own unlock. Off, nothing
differs from without the option.

**The Gasoline Tank** (`GAS_ITEM`) never moves. With `remix_unlockables` on and
at least one of the Chainsaw and Rock Drill not kept home (`gas_removed`), it is
out of the game: hidden and its pickup row sent out of reach, as an item that
left for another area (`xmap_local`), and its spot joins the shared list of
spots weapons and unlockables leave for -- whatever goes there leaves nothing on
its own spot in exchange. The Chainsaw and Rock Drill then need no fuel, but
still wait for the tank's own unlock: Next Fear (`EventFlag_62`, set as it
starts; `fuel_unlock`). Moved, their arrivals require that flag (not drawn
before it), and one kept home has it put in place of the fuel on its rows
(`Pc_Remix_OnMapLoad`; the map still shows it in its window, as the original
does). With `remix_unlockables_always`, they require nothing. With both kept home, the tank is on its spot
and the fuel rule is the original's. It isn't a name `remix_fixed_items` takes. That fuel requirement is also why a moved copy
used to be invisible: a moved key is drawn only while one of its pickup rows can
fire, and theirs couldn't until the tank was used -- the map draws them at home
regardless.

**Kept items** (`remix_fixed_items`). Names from `REMIX_ITEM_NAMES` (generated:
every key item, weapon and unlockable the remix can move). A listed key item is
pinned on its map like a locked bonus item (its spot takes nothing else); a
listed item is never the leaving side of a between-areas trade, and a listed key
never the arriving side's host. Your starting gun's pickup is the exception: it
is the Handgun then, and must still turn up. With `remix_keys` off, no key item
is a trade's host either.

### Enemies

`Pc_Remix_EnemyReroll` runs in `Game_NpcRoomInitSpawn` as a spawn row spawns,
after the map has resolved the row's type (Nowhere sets its group types from
code, so a load-time table edit would miss them). The new type is a pure
function of (seed, map, row): a row is always the same monster for a seed, and
the save's per-row "killed" bits keep their meaning. Only plain room spawns are
rerolled: grey children, mumblers, creepers, rompers, groaners, wormheads,
larval stalkers and stalkers with entry state 3, air screamers and night
flutters hovering (3) or gliding in (6), and every nurse and doctor (their state
only encodes look and stance: `1 + palette + 3 * look + 9 * stance`). Hanged
scratchers are never rerolled, so the sewers keep their own; another spawn there
can still become one, on the floor (6) or the ceiling (9). Each pick
gets its own type's entry state (3; flyers 3 or 6, the street maps' own; nurses
14 or 18; doctors 10, the hospital's own; Groaner_Init only stands the dog up at
3). The pool is chosen at map load: Nowhere by map index (`map7_*`), then from
the map's `MAP_INFOS` row: interior `SU`/`HU` (Otherworld indoors), `SC` (normal school), interior flag, `THR`/`SPR`/`RSR` (streets),
`DR`/`DRU` (sewers), `APR`/`APU` (amusement park) or `SPU`/`RSU` (Otherworld streets). A foreign type comes from the
global chara pool and chara_global.dll (`docs/Global_Chara_Pool.md`); a type the
pool can't supply is skipped for the next in the pool.

**Your own mix per kind of area.** `remix_enemies_<zone>` (`outside`,
`outside_otherworld`, `school`, `otherworld_school`, `hospital` -- the normal and
the Otherworld hospital -- `buildings` -- every other interior --, `nowhere`,
`park`, `sewers`) replaces that kind of area's built-in pool with the monsters it
lists (`ENEMY_TYPES` names: GreyChild, Mumbler, Creeper, Groaner, Wormhead,
Romper, AirScreamer, NightFlutter, LarvalStalker, Stalker, PuppetNurse,
PuppetDoctor, HangedScratcher), each with its own entry states as above.
`default` (or empty) keeps the built-in pool world by world, `none` leaves the
area's spawns vanilla. The launcher's Enemies tab is a monster x area grid of
these, with a Reset button; it writes `default` for a column left at the
built-in mix (for the hospital and the sewers, both worlds' mixes together).
Flyers and rompers are made for open ground and hanged scratchers for the
sewers' ceilings, so the launcher warns when they are picked elsewhere.
`remix_enemies_sewer_scratchers = 1` also rerolls the sewers' own hanged
scratchers (floor 6 or ceiling 9); the launcher warns that it may break the area.

**Their own sounds.** A monster's SFX ids name programs in "the" ambient bank
(libsd VAB slot 2), which every map replaces with its own, so a monster outside
its home maps would play some other sound or none. For each pool type not
native to the map, `pc_remix_sfx.c` picks one of its home maps' ambient banks
(`REMIX_CHARA_BANKS` in the generated `pc_remix_sfx_data.h`: the banks of the
maps whose `charaGroupIds` list it, preferring banks that serve several types),
reads it off the disc image at map load and keeps it cached. Where the
samples go depends on the sound backend, probed at map load by asking the SPU
for a transfer address above 512 KiB:

- **2 MiB of sample memory** (the legacy OpenAL SPU, or the software SPU with
  `pc_port/patches/psycross_spu_ext_ram.patch` applied): every home bank is
  loaded whole above 512 KiB at map load, into a spare libsd VAB (4..10). The
  game never goes past 512 KiB, so nothing it loads can touch them. This is the
  mode that gives every monster its sounds.
- **512 KiB** (the stock software SPU, which wraps every address to 512 KiB, so
  writing above it once overwrote BASE.VAB and changed Harry's footsteps): only
  the samples the monster's own sound ids use (`FAMILY_SFX`) are packed into the
  unused tail of the ambient slot, from the end of this map's own ambient bank
  up to the music bank's fixed start (0x58F50), under a VAB whose VAG size table
  is rewritten so each packed sample sits where libsd looks for it. Packing
  waits until the map's own ambient bank has finished loading and is redone
  when it changes. That tail is often only a few dozen KiB, so most foreign
  types don't fit and keep the map's own sounds.

When a sound is played from a monster's position
(`Sfx_WithFlagsAndPitchPlay`, `Sfx_WithPitchPlay`,
`Sfx_WithFalloffAndPitchPlay`, and their twins in `3d_audio.c`,
`func_8005DC3C` / `func_8005DD44`, which most monsters' own cries go through:
the Groaner, Wormhead, Romper, Stalker and Grey Child, Larval Stalker), the
emitter is latched (only when the sound's position pointer is inside a foreign
monster; never ground-material footsteps); an ambient-bank sound from it keys
on from its borrowed set, and `Sd_SfxStopStep` stops it there too. A sound
played from a point of the monster's own (the Hanged Scratcher's claws) is
latched to the NPC whose AI update is running (`Pc_RemixSfx_Updating`, set
around the update call in `npc_main.c`). With the extended SPU, if the game
closes or reuses one of the borrowed VAB slots (`Sd_AudioStop` closes 0-4), the
banks are loaded again on the next foreign sound, at most 4 times a map.

### Hooks

Every hook in engine code is behind its own toggle check at the call site, so
with remix off the game runs exactly the base code path (the only extra work
left is the check itself). Each `Pc_Remix_*` / `Pc_RemixSfx_*` name the engine
calls is a forwarder in `pc_remix_host.c` into the add-on's `s_RemixAddon`
table (`RemixAddon_Get`, `remix_addon.c`); the add-on's own implementations are
renamed `Rmx_*` by the force-included `remix_addon_names.h`. Without the DLL,
every check answers "off" and every remap passes its input through.

| Site | Gate | Why |
|---|---|---|
| `main_pc.c` boot | none | `Pc_Remix_Init`: load the add-on DLL, which reads `randomizer.cfg` and adds its PC Options page |
| `options.c` PC Options pages | an add-on registered a page | `PcOpt_AddonPageSet`: the add-on's rows, after Controls; values saved through its callback |
| `game_boot.c` `GameBoot_MapLoad` tail | `Pc_Remix_AnyOn()` | `Pc_Remix_OnMapLoad`: copy the live header (inherits the language patch), rewrite doors and pickup points, wrap `updateWorldObjects`. Off: `Pc_Remix_Off` releases anything a remixed map left (key model copies) |
| `world_draw.c` `WorldGfx_ObjectAdd` top | `Pc_Remix_MapActive()` | `Pc_Remix_ObjectAddFilter`: hide a moved key's original-pose draw |
| `npc_main.c` `Game_NpcRoomInitSpawn`, as a row spawns | `Pc_Remix_EnemiesOn()` | `Pc_Remix_EnemyReroll`: the row's monster type for the seed |
| `sfx.c` `Sfx_WithFlagsAndPitchPlay` / `Sfx_WithPitchPlay` / `Sfx_WithFalloffAndPitchPlay` | `Pc_RemixSfx_On()` | latch the emitting monster |
| `3d_audio.c` `func_8005DC3C` / `func_8005DD44` | `Pc_RemixSfx_On()` | latch the emitting monster (the same, for the func_8005D* callers) |
| `npc_main.c` around each NPC's AI update | `Pc_RemixSfx_On()` | `Pc_RemixSfx_Updating`: whose update is running |
| `sd_call.c` `Sd_PlaySfx` / `func_80046620` / `Sd_SfxStopStep` | `Pc_RemixSfx_On()` | key a foreign monster's sound on (and off) from its borrowed samples |
| `sd_call.c` `Pc_Sd_AmbientState` (new, PC only) | none (read-only) | lets `pc_remix_sfx.c` see the driver's loading flag and last ambient bank |
| `player_control.c` `GameFs_PlayerMapAnimLoad` | `Pc_Remix_EnemiesWanted()` (`remix_enemies`) | `Pc_HopInject`: borrow the creeper-bite feet hop for maps without it |
| `item_screens_1.c` `Inventory_HyperBlasterFunctionalTest` | the save has no UFO ending | `Pc_RemixHyper_Works` (`pc_remix_hyper.c`): fires as unlocked with `remix_unlockables_always` |
| `item_screens_2.c` the inventory opening's Hyper Blaster gift | `Inventory_HyperBlasterUnlockTest() == 1` | `Pc_RemixHyper_Grant` (`pc_remix_hyper.c`): 0 while the randomizer has it in the world or as the starting gun |
| `player_control.c` damage states (`DamageFeet*`/`DamagePush*`) | `Pc_Remix_EnemiesWanted()` | re-apply the borrowed hop's anim slots; a reaction with no anim on the map falls back to the torso hit or is skipped instead of freezing |

The copy uses only existing engine entry points (`Ipd_FreeChunkFind`,
`Ipd_LoadStart`, `IpdHeader_LoadStateGet`, `IpdHeader_PC_IsReformatted`,
`Lm_ModelFind`, `Texture_RefClear`); it adds no hook.

The DLL's tables are never written. Before touching a map, the mode checks every
row it will rewrite against the live table (row index, `sysState`, doorway,
`eventParam`, pickup flag). On any mismatch (a modded DLL, or a source edit
without regenerating) it logs `[RANDOMIZER] ... does not match` and leaves the map
vanilla.

## Regenerating the data

```
python3 pc_port/tools/gen_remix_data.py      # writes pc_port/addons/remix/include/pc_remix_data.h
```

The generator checks its map indices against the `e_MapIdx` enum, and reads
`MAP_EVENTS[]` anchored on its own declaration. This matters for map1_s00, which
declares `g_CommonWorldObjectPoses` first in the same file. (The
`sh_map_source.py::map_events` helper starts at the first `{` in the file, so
its map1_s00 rows are offset by five; don't use it where row indices matter.)

## Tests

```
cmake -S pc_port -B build -DBUILD_TESTING=ON && cmake --build build --target remix_layout_test && ctest --test-dir build -R remix
# or directly:
cc -I pc_port/addons/remix/include pc_port/tests/remix_layout_test.c pc_port/addons/remix/src/pc_remix_layout.c && ./a.out
```

`remix_layout_test` runs every covered map through 2000 seeds and asserts all of
the guarantees above, plus determinism and pickup spot uniqueness and spread.

## Known gaps

- **Untested in-game.** Everything above has been checked against the real source
  tables and the real compiled `MAP_EVENTS` bitfields, but not on a running game.
  Watch `SilentHill.log` for `[RANDOMIZER]` lines on the first runs.
- The paper map and its door arrows still describe the original building, and
  its marks for the Key of Lion and Key of Scarecrow point at their original
  spots.
- A common pickup moved to another common pickup's place that the map picked
  up through a short "reach across" trigger line (TouchObbFacing, angled for
  its own desk) uses the plain facing trigger instead: stand within 0.8 m and
  face it, from any side. A key on another item's place uses that item's own
  trigger (see Reachability). Back at its own spot an item gets its vanilla
  trigger again.
- A moved common pickup is drawn with the pose of the pickup that was at its
  new spot (on a key's spot: on the floor there). A moved key keeps its own
  height, so on a lower or higher surface than its own it can float or sink a
  little.
- Monster spawns and puzzle objects stay in their original rooms. Key items move
  only within their zone, and only those picked up by a plain `Event_ItemTake`;
  ones with scripted pickups (the radio, flashlight, Antique Shop key, Sewer
  key and similar) stay put. On exterior maps an item whose model is part of the
  ground geometry isn't copied, and keeps its own cell.
- Randomizer weapons is untested in-game. The Handgun lying in the world uses the
  model Harry holds, as he holds it, turned at random; how it sits on the
  floor hasn't been seen yet. Turning the mode on mid-game (after the cafe) keeps
  Cybil's Handgun, and your seed's starting gun's pickup still becomes the
  Handgun, which then gives its ammo: turn it on before a new game.
- A door to another floor is a map load, a little longer than a room change.
  Rooms across floors is untested in-game: watch `SilentHill.log` for
  `rooms across floors` lines.
- "Keys past their zone" assumes a flag that opened a door stays set. A door
  that locks again behind you through a disabled flag never counts as open, but
  one closed by a script that clears its required flag would.
- Between areas: a key that arrived uses the English take message whatever the
  game language, and the Key of Woodman taken away from its lane doesn't play
  its crouch or set its paper-map mark. Its source area's paper-map marks still
  point at the original spots.
- Seven more items are in the tables but have nowhere to go yet: their zone (a
  room behind one locked door, or a map with no free doors) holds no other spot.
  These are the police station's Shotgun, Safe Key, Examination Room key, Video
  Tape, Bird Cage key, Ankh and Disk of Ouroboros (nothing more is provably
  reachable from their rooms at pickup time). The Katana and Channeling Stone are optional,
  but their map's only other item spots are a cluster the items mode can't move,
  so they stay too.
- Animated pickups stay put, except the Key of Woodman: where it lands, Harry
  still crouches down for it as he did in its own lane.
- Rooms are camera volumes. The design rests on one assumption: a single camera
  switch box never spans two physically separate rooms.
