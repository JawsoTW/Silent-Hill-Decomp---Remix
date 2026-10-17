#!/usr/bin/env python3
"""Generate pc_port/addons/remix/include/pc_remix_data.h for the Remix mode (docs/Remix_Mode.md).

Remix rebuilds an interior's room graph and moves its common pickups. Everything
it needs is harvested here from the map sources, so the runtime never has to
guess:

DOORS. Interior rooms are islands scattered across the map plane, joined only by
`SysState_LoadRoom` rows. Every such door has a twin on the far side: door A's
arrival mapPoint sits on door B's doorway and vice versa. For a door X, the
"landing" -- where you appear when you come INTO X's room through X -- is
therefore its twin's (eventParam, mapIdx-as-BGM, field_8_24-camera-rail). Joining
A to C is then just: A gets C's landing, C gets A's landing. No coordinates are
invented.

A door is FREE (rewirable) only if it is ungated and plain:
  - no requiredEventFlag, no requiredItemId, not Item-activated;
  - its disabledEventFlag is either none or a >= 867 paper-map mark (Event_Update
    ignores those for gating unless the row is Exclusive);
  - it is the only row on its doorway (a sibling row means a locked/cutscene state);
  - it has exactly one mutual twin, which is also free.
Everything else is FIXED and keeps its vanilla destination.

ROOMS are the camera-switch volumes (VC_ROAD_DATA.lim_sw), one node per box; a
doorway belongs to the box it sits in. A real room spanning several boxes just
becomes several nodes, which is always safe (see BOX_TOUCH). A door is only free if
it and its landing resolve to the same box, and a map is only remixed if no vanilla
free pair joins a box to itself.

ZONES are the connected components of rooms over FREE pairs. The runtime rewires
each zone independently, keeping each room's free-door count and keeping the zone
connected. Fixed doors are untouched, so the zone-to-zone structure -- which is
what keys, story flags and locked doors gate -- is exactly vanilla. Progression
cannot be broken by the remix.

PICKUPS are the common items (health / ammo) a map draws from
g_CommonWorldObjects[] behind `if (!Savegame_EventFlagGet(flag))`. Each is tied to
its EventCallback row through that flag. SPOTS are candidate positions: the
pickups' own positions plus the map's authored monster spawn positions, kept only
if they lie inside a camera volume (somewhere the player can stand) and clear of
every doorway.

Run from the repo root:  python3 pc_port/tools/gen_remix_data.py
"""

import glob
import importlib.util
import math
import struct
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "pc_port", "addons", "remix", "include", "pc_remix_data.h")

_spec = importlib.util.spec_from_file_location("gen_rando_data", os.path.join(HERE, "gen_rando_data.py"))
rd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rd)

MIN_FREE_DOORS = 4
PAIR_EPS = 4.0          # a door's arrival point vs. its twin's doorway
ROOM_EPS = 3.0          # a doorway may sit this far outside its camera box
# Camera boxes are never merged into larger rooms. Treating one physical room as
# several nodes is always safe -- a node graph that is connected stays connected
# when nodes are glued together -- whereas merging two boxes that only touch across
# a wall could hide a real disconnection. Costs ~6% of remixable doors.
BOX_TOUCH = None
SPOT_DOOR_CLEAR = 3.0   # no pickup spot this close to a doorway
SPOT_DEDUPE = 1.5
SPOT_ROOM_EPS = 1.0
MAX_SPOTS = 64
DISABLED_FLAG_MARK_MIN = 867  # Event_Update: below this, disabledEventFlag gates


def q(s):
    m = re.search(r'Q\d+\(\s*(-?[\d.]+)f?\s*\)', s or '')
    return float(m.group(1)) if m else 0.0


def q12(v):
    """Exactly what the C Q12() macro yields for a `<v>f` literal: (s32)(float * 4096),
    single-precision and truncated toward zero. Rounding instead would put a moved
    item 1/4096 unit off its authored spot -- harmless in game, but it breaks exact
    position checks."""
    f = struct.unpack('f', struct.pack('f', v))[0]
    return int(f * 4096.0)


def load_flags():
    flags = {}
    text = open(os.path.join(REPO, 'include', 'event_flags.h')).read()
    for m in re.finditer(r'(EventFlag_\w+)\s*=\s*(\d+)', text):
        flags[m.group(1)] = int(m.group(2))
    return flags


FLAGS = load_flags()


def flag_value(tok):
    if tok is None:
        return 0
    tok = tok.strip()
    if tok.isdigit():
        return int(tok)
    if tok not in FLAGS:
        # The decomp spells a few numbered flags oddly (`EvnetFlag_196`).
        m = re.match(r'\w*Flag_(\d+)$', tok)
        if m:
            return int(m.group(1))
        sys.exit("unknown event flag %r" % tok)
    return FLAGS[tok]


def int_field(block, name, default=0):
    v = rd.field(block, name)
    if v is None:
        return default
    v = v.strip()
    if v.isdigit():
        return int(v)
    return None


def item_enum():
    text = open(os.path.join(REPO, 'include', 'bodyprog', 'items.h')).read()
    return {m.group(1): int(m.group(2)) for m in re.finditer(r'(InvItemId_\w+)\s*=\s*(\d+)', text)}


ITEM_IDS = None


def item_id(tok):
    global ITEM_IDS
    if ITEM_IDS is None:
        ITEM_IDS = item_enum()
    return ITEM_IDS[tok]


def mapidx_enum():
    text = open(os.path.join(REPO, 'include', 'bodyprog', 'map', 'map.h')).read()
    return {m.group(1).lower(): int(m.group(2))
            for m in re.finditer(r'MapIdx_(MAP\d_S\d\d)\s*=\s*(\d+)', text)}


def camera_rooms(m):
    path = os.path.join(REPO, 'src', 'maps', m, 'vc_road_data.h')
    boxes = []
    if os.path.exists(path):
        for b in rd.top_level_braces(open(path).read()):
            sw = re.search(r'\.lim_sw\s*=\s*\{(.*?)\}', b, re.S)
            if sw:
                s = sw.group(1)
                boxes.append((q(rd.field(s, 'min_hx')), q(rd.field(s, 'max_hx')),
                              q(rd.field(s, 'min_hz')), q(rd.field(s, 'max_hz'))))
    parent = list(range(len(boxes)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for i, a in enumerate(boxes if BOX_TOUCH is not None else []):
        for j in range(i):
            b = boxes[j]
            if (a[0] <= b[1] + BOX_TOUCH and b[0] <= a[1] + BOX_TOUCH and
                    a[2] <= b[3] + BOX_TOUCH and b[2] <= a[3] + BOX_TOUCH):
                parent[find(i)] = find(j)

    def room_of(p, eps):
        best, bd = None, 1e9
        for k, b in enumerate(boxes):
            dx = max(b[0] - p[0], 0.0, p[0] - b[1])
            dz = max(b[2] - p[1], 0.0, p[1] - b[3])
            d = math.hypot(dx, dz)
            if d < bd:
                best, bd = find(k), d
        return best if bd <= eps else None

    room_of.boxes = boxes
    return room_of


KEY_TOUCH = 0.5  # camera boxes this close are treated as one walkable area for key zoning
ISLAND_ZONE_BASE = 128  # key zones for rooms behind one gated door; door zones are < 128
MAX_KEYSLOTS = 64


def box_zones(boxes, labels):
    """Zone for every camera box, not just the ones holding a free door.

    Touching boxes are grouped. `labels` maps a box to a label: a free door's zone,
    or a unique label for every gated / scripted / map-exit door box. A group takes
    a zone only if every label in it is that one zone. So a room entered through a
    locked door can never borrow a neighbour's zone across a wall: its own door box
    carries a conflicting label and the whole group is left unknown."""
    n = len(boxes)
    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for i in range(n):
        a = boxes[i]
        for j in range(i):
            b = boxes[j]
            if (a[0] <= b[1] + KEY_TOUCH and b[0] <= a[1] + KEY_TOUCH and
                    a[2] <= b[3] + KEY_TOUCH and b[2] <= a[3] + KEY_TOUCH):
                parent[find(i)] = find(j)
    group_labels = {}
    for box, labs in labels.items():
        group_labels.setdefault(find(box), set()).update(labs)
    # A group behind exactly one gated / scripted door is a zone of its own: every
    # box in it opens up together, the moment that one door does.
    fresh = {}
    for g in sorted(group_labels, key=lambda g: sorted(map(str, group_labels[g]))):
        labs = group_labels[g]
        if len(labs) == 1 and not isinstance(next(iter(labs)), int):
            fresh[g] = ISLAND_ZONE_BASE + len(fresh)
    out = {}
    for i in range(n):
        labs = group_labels.get(find(i), set())
        if len(labs) == 1 and isinstance(next(iter(labs)), int):
            out[i] = next(iter(labs))
        elif find(i) in fresh:
            out[i] = fresh[find(i)]
    return out


def chara_spawns(m):
    path = os.path.join(REPO, 'src', 'maps', m, 'chara_spawns.h')
    if not os.path.exists(path):
        return []
    out = []
    pat = re.compile(r'\{\s*Q12\(\s*(-?[\d.]+)f?\s*\)\s*,\s*\w+\s*,\s*[^,]+,\s*(\d+)\s*,\s*\w+\s*,\s*Q12\(\s*(-?[\d.]+)f?\s*\)\s*\}')
    for mm in pat.finditer(rd.strip_comments(open(path).read())):
        if int(mm.group(2)) != 0:
            out.append((float(mm.group(1)), float(mm.group(3))))
    return out


def pickup_models(m):
    """flag token -> g_CommonWorldObjects[] index, from the map's world-object update."""
    return {k: v[0] for k, v in pickup_draws(m).items()}


def pickup_draws(m):
    """flag token -> (g_CommonWorldObjects[] index, pose symbol, pose index): the
    s_Pose the map draws the pickup with (its authored height and turn), or
    (model, None, 0) when that pose isn't an exported s_Pose."""
    # Most maps pass &g_CommonWorldObjectPoses[i]; the sewers pass their own pose
    # globals and spell model 0 as plain `g_CommonWorldObjects`. Either way the
    # runtime hides the map's draw by its flag, not by the pose.
    pat = re.compile(r'Savegame_EventFlagGet\(\s*(EventFlag_\w+)\s*\)\s*\)\s*\{\s*'
                     r'WorldGfx_ObjectAdd\(\s*(?:&g_CommonWorldObjects\[(\d)\]|g_CommonWorldObjects)\s*,'
                     r'\s*(&(\w+)(?:\[(\d+)\])?(?:\.position|->position|\s*,))?')
    hdrs = ''.join(rd.strip_comments(open(f, errors='ignore').read())
                   for f in glob.glob(os.path.join(REPO, 'include', 'maps', '*', m + '.h'))
                   + glob.glob(os.path.join(REPO, 'src', 'maps', m, '*.c')))
    out = {}
    for f in glob.glob(os.path.join(REPO, 'src', 'maps', m, '*.c')):
        for mm in pat.finditer(rd.strip_comments(open(f, errors='ignore').read())):
            sym, idx = mm.group(4), mm.group(5)
            decl = sym and re.search(r'^\s*extern\s+s_Pose\s+' + sym + (r'\s*\[' if idx else r'\s*[\[;]'), hdrs, re.M)
            out[mm.group(1)] = (int(mm.group(2) or 0), sym if decl else None, int(idx or 0))
    return out


COMMON_ITEMS = {'InvItemId_HealthDrink', 'InvItemId_FirstAidKit', 'InvItemId_Ampoule',
                'InvItemId_HandgunBullets', 'InvItemId_ShotgunShells', 'InvItemId_RifleShells'}
CELL = 40.0  # CHUNK_CELL_SIZE: one IPD cell, the unit chunk-local models resolve in
# Bonus items nothing in the story needs: missing one can never block progress, so
# they may go to any item spot on their map, zone or not. Their pickup rows keep
# their unlock condition (requiredEventFlag), so a moved one still only appears
# once it is unlocked.
OPTIONAL_ITEMS = {'InvItemId_Katana', 'InvItemId_ChannelingStone', 'InvItemId_RockDrill', 'InvItemId_Chainsaw'}
# Items whose pickup row is always live but whose room only opens once a bonus is
# unlocked (the Katana's door in the town needs EventFlag_57, set by the
# next-fear ranking extras). Until the flag is set they stay put and their spot
# takes nothing else, so nothing lands in a room the player can't enter.
UNLOCK_FLAGS = {'InvItemId_Katana': 'EventFlag_57'}
# Unlockables (remix_unlockables): the next-fear bonus items. They never move
# within their map; with the toggle on, each leaves home for one of every
# health/ammo spot in the game, as the weapons do, and still shows (and is
# taken) only once unlocked -- except that the Chainsaw and Rock Drill need no
# Gasoline Tank (FUEL_ITEMS: their pickup rows' required flag is the fuel).
UNLOCKABLE_ITEMS = {'InvItemId_Katana', 'InvItemId_ChannelingStone', 'InvItemId_Chainsaw', 'InvItemId_RockDrill'}
FUEL_ITEMS = {'InvItemId_Chainsaw', 'InvItemId_RockDrill'}
# The fuel itself: with remix_unlockables (and the Chainsaw or Rock Drill
# moving) it is taken out of the game, and its spot joins the spots weapons
# and unlockables may go to. It never moves.
GAS_ITEM = 'InvItemId_GasolineTank'
NO_ZONE = 254  # slot outside any zone: only optional items may use it

# Where a street key may trade places with a pickup: only pickups the player can
# reach before the place the key is used. Old Silent Hill's Key of Lion and Key
# of Scarecrow open the door in the Levin St house (event at (111.9, 216.5)).
# Before that, the town is the block the paper map's first-visit crosses close
# off (Finney St at x -154 and 180, Matheson St at -164, Levin St at z -49,
# Bachman Rd at z -36 and 220, Ellroy St at z -40), plus the alley outside the
# cafe and the lane behind it (the cafe's exit and its ungated door at
# (-110, 224.2)), and the house's own rooms up to that door (the House Key from
# the doghouse lets you in). The pickups there:
#   (185.3, 114.8)  on Finney St, the Key of Lion's own corner
#   (-190.4, 301.4) the lane by the steel pipe, behind the cafe's alley
#   (112.8, 219.2), (117.6, 219.4), (115.7, 216.3), (127.2, 218.6)  in the house
#   (98.9, -35.5)   on Ellroy St, beside the Key of Scarecrow
#   (55.6, -34.9)   between Bachman Rd and Ellroy St, north of their crosses
#   (-62.5, -32.5)  between Levin St and Bachman Rd, north of their crosses
#   (21.1, 17.8), (21.6, 63.5)  on Bachman Rd by the cafe
#   (-188.9, 375.6) the Key of Woodman's lane
# Everything else on the map is behind the house's door (the north lane, the
# school side, Bloch St and south, the K. Gordon house), or past a first-visit
# roadblock (Midwich St, Matheson St west of x -164). The street keys' own
# spots are candidates too.
STREET_SWAP_SPOTS = {
    'map2_s00': [(185.3, 114.8), (-190.4, 301.4),
                 (112.8, 219.2), (117.6, 219.4), (115.7, 216.3), (127.2, 218.6),
                 (98.9, -35.5), (55.6, -34.9), (-62.5, -32.5), (21.1, 17.8), (21.6, 63.5),
                 (-188.9, 375.6)],
}

# Key items that always stay where they are: the cafe's kitchen knife,
# flashlight and pocket radio (the game's opening, as the original has it).
STAY_PUT_ITEMS = {'InvItemId_KitchenKnife', 'InvItemId_Flashlight', 'InvItemId_PocketRadio',
                  # The hospital's Plastic Bottle and Basement Key (map3_s01) are hidden
                  # models the room shows only at home (BOT_HIDE, KEY_HIDE); a copy of
                  # either draws nothing.
                  'InvItemId_PlasticBottle', 'InvItemId_BasementKey'}

# Key pickups that aren't a plain Event_ItemTake but may still move: the pickup
# is a scripted crouch-and-take whose only state is the item and its flag, so it
# plays the same wherever the key lies. {map: {event func: (item, flag)}}
EXTRA_KEY_PICKUPS = {
    # Key of Woodman, in the lane behind the cafe: Harry reaches down for it.
    'map2_s00': {'func_800E9A74': ('InvItemId_KeyOfWoodman', 'EventFlag_M2S00_PickupKeyOfWoodman'),
                 # The steel pipe in the sketchbook's lane: a crouch, then "Take it?".
                 # Out on the street it is a street item like the three keys.
                 'MapEvent_SteelPipeTake': ('InvItemId_SteelPipe', 'EventFlag_M2S00_PickupSteelPipe')},
    # The Otherworld school's Shotgun, in the bathroom behind the corpse: Harry
    # crouches for it. Its pickup row needs the corpse moved (EventFlag_96); a
    # moved Shotgun shows only once that is done, so it is never seen untakeable.
    'map1_s02': {'func_800DBFC8': ('InvItemId_Shotgun', 'EventFlag_M1S02_PickupShotgun')},
    # The Hunting Rifle: a plain take, after the game's usual "no time passes
    # while a monster is near" check.
    'map4_s03': {'func_800D6704': ('InvItemId_HuntingRifle', 'EventFlag_M4S03_PickupHuntingRifle')},
}
# Weapons (remix_weapons): each may turn up at any health/ammo spot in the game,
# or on a key's spot when that key can safely take the weapon's place instead.
WEAPON_ITEMS = {'InvItemId_KitchenKnife', 'InvItemId_SteelPipe', 'InvItemId_Hammer', 'InvItemId_Axe',
                'InvItemId_Katana', 'InvItemId_Chainsaw', 'InvItemId_RockDrill', 'InvItemId_Shotgun',
                'InvItemId_HuntingRifle'}

# Items that trade places like the street keys even though their spot is in a
# zone of its own: they go to a spot reachable before the Levin St house, and
# their own spot takes a pickup, a street key or an item from another area.
STREET_ITEMS = {'map2_s00': ('InvItemId_SteelPipe',)}
INTERIOR_MAP_TYPES = {'SC', 'SU', 'ER', 'ER2', 'HP', 'HU'}  # MAP_INFOS rows with MapFlag_Interior


def is_exterior(m):
    hdr = os.path.join(REPO, 'src', 'maps', m, m + '_header.c')
    mm = re.search(r'\.mapInfo\s*=\s*&MAP_INFOS\[MapType_(\w+)\]', open(hdr).read()) if os.path.exists(hdr) else None
    return bool(mm) and mm.group(1) not in INTERIOR_MAP_TYPES


# Bodies an item pickup function may have besides its one Event_ItemTake: only
# event-flag reads and writes (the street keys also mark the paper map) and
# switching the item itself on or off (the cafe's radio and flashlight), so
# moving where the pickup happens changes nothing else.
_FLAG_ONLY = re.compile(r'(\s|if|else|[(){};!]|&&|\|\||Savegame_EventFlag(Get|Set|Clear)|EventFlag_\w+'
                        r'|g_SavegamePtr->itemToggleFlags\s*\|=\s*ItemToggleFlag_\w+|Game_TurnFlashlightOff\(\s*\))*')


def _bodies(src):
    for mm in re.finditer(r'\bvoid\s+(\w+)\s*\(\s*void\s*\)\s*\{', src):
        i, d = mm.end(), 1
        while d and i < len(src):
            d += {'{': 1, '}': -1}.get(src[i], 0)
            i += 1
        yield mm.group(1), src[mm.end():i - 1]


def key_pickups(m):
    """Key-item pickups: an event func whose body is one
    Event_ItemTake(<non-common item>, ..., <flag>, msg) plus, at most, event-flag
    reads and writes, and whose model the map's world-object update draws as
    `if (!Savegame_EventFlagGet(flag)) { WorldGfx_ObjectAdd(&<sym>.object, &<sym>.position, ...`
    (or `&<sym>[i].object, &<sym>[i].position`) with <sym> a non-static
    s_WorldObjectPose / s_WorldObjectPlacement (or an array of them) exported from
    the map DLL, model at offset 0.
    Returns {func name: (item, flag token, sym, is_pose, index)}."""
    src = rd.strip_comments(''.join(open(f, errors='ignore').read()
                                    for f in glob.glob(os.path.join(REPO, 'src', 'maps', m, '*.c'))))
    hdrs = src + ''.join(rd.strip_comments(open(f, errors='ignore').read())
                         for f in glob.glob(os.path.join(REPO, 'include', 'maps', '*', m + '.h')))
    out = {}
    extra = EXTRA_KEY_PICKUPS.get(m, {})
    for fn, body in _bodies(src):
        mm = re.match(r'\s*Event_ItemTake\(\s*(InvItemId_\w+)\s*,[^,]+,\s*(EventFlag_\w+)\s*,[^;]+\);(.*)$',
                      body, re.S)
        if fn in extra:
            item, flag = extra[fn]
            if item not in body or flag not in body:
                continue
            mm2 = (re.search(r'MapMsg_DisplayAndHandleSelection\(\s*\w+\s*,\s*(\d+)', body) or
                   re.search(r'Event_DisplayMapMsg\(\s*\w+\s*,\s*(\d+)', body) or
                   re.search(r'Event_ItemTake\([^,]+,[^,]+,[^,]+,\s*(\d+)\s*\)', body))
            msg = int(mm2.group(1)) if mm2 else None
        elif not mm:
            continue
        else:
            item, flag, rest = mm.groups()
            if item in COMMON_ITEMS or _FLAG_ONLY.fullmatch(rest) is None:
                continue
            mm2 = re.match(r'\s*Event_ItemTake\([^,]+,[^,]+,[^,]+,\s*(\d+)\s*\)', body)
            msg = int(mm2.group(1)) if mm2 else None
        # (The Gasoline Tank's draw also checks its unlock: `&& Savegame_EventFlagGet(...)`.)
        draws = re.findall(r'Savegame_EventFlagGet\(\s*' + flag + r'\s*\)\s*'
                           r'(?:&&\s*Savegame_EventFlagGet\(\s*\w+\s*\)\s*)?\)\s*\{\s*'
                           r'WorldGfx_ObjectAdd\(\s*&(\w+)(?:\[(\d+)\])?\.object\s*,\s*&\1(?:\[\2\])?\.position\s*,', src)
        if len(set(draws)) != 1:
            continue
        sym, idx = draws[0]
        decl = re.search(r'^\s*(static\s+)?(?:extern\s+)?(s_WorldObjectPose|s_WorldObjectDesc|s_WorldObjectPlacement)\s+' + sym +
                         (r'\s*\[[^\]]*\]\s*(=|;)' if idx else r'\s*;'), hdrs, re.M)
        if not decl or decl.group(1):
            continue
        # s_WorldObjectDesc is s_WorldObjectPose under another name (maps/shared.h).
        out[fn] = (item, flag, sym, decl.group(2) != 's_WorldObjectPlacement', int(idx or 0), msg)
    return out


def map_events(m):
    """MAP_EVENTS[] rows in source order. Anchored on the MAP_EVENTS declaration:
    the row index is what the runtime rewrites, and map1_s00 declares another array
    (g_CommonWorldObjectPoses) ahead of it in the same file."""
    files = glob.glob(os.path.join(REPO, 'src', 'maps', m, '*events_data.c'))
    if not files:
        return []
    t = rd.strip_comments(open(files[0]).read())
    decl = re.search(r'\bMAP_EVENTS\s*\[[^\]]*\]\s*=\s*\{', t)
    if not decl:
        return []
    start, depth = decl.end(), 1
    i = start
    while depth and i < len(t):
        depth += {'{': 1, '}': -1}.get(t[i], 0)
        i += 1
    return rd.top_level_braces(t[start:i - 1])


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def analyze(m):
    pts = [(q(rd.field(p, 'positionX')), q(rd.field(p, 'positionZ'))) for p in rd.map_points(m)]
    blocks = map_events(m)
    rows = []
    for i, e in enumerate(blocks):
        tt = rd.field(e, 'triggerType')
        if tt == 'TriggerType_EndOfArray':
            break
        rows.append(dict(
            idx=i, block=e, tt=tt, at=rd.field(e, 'activationType'),
            ss=rd.field(e, 'sysState'), poi=int_field(e, 'pointOfInterestIdx'),
            ep=int_field(e, 'eventParam'), req=flag_value(rd.field(e, 'requiredEventFlag')),
            dis_tok=rd.field(e, 'disabledEventFlag'), dis=flag_value(rd.field(e, 'disabledEventFlag')),
            item=rd.field(e, 'requiredItemId'), rail=int_field(e, 'field_8_24'),
            bgm=rd.bgm_value(rd.field(e, 'mapIdx'))))
    if not rows or not pts:
        return None

    by_poi = {}
    for r in rows:
        by_poi.setdefault(r['poi'], []).append(r)

    room_of = camera_rooms(m)

    def gated(r):
        if r['req'] or (r['item'] not in (None, '0', 'InvItemId_Unequipped')):
            return True
        if r['at'] == 'TriggerActivationType_Item':
            return True
        if r['dis'] and (r['dis'] < DISABLED_FLAG_MARK_MIN or r['at'] == 'TriggerActivationType_Exclusive'):
            return True
        return len(by_poi[r['poi']]) > 1

    doors = [r for r in rows
             if r['ss'] == 'SysState_LoadRoom' and r['tt'] != 'TriggerType_None'
             and r['poi'] is not None and r['ep'] is not None
             and r['poi'] < len(pts) and r['ep'] < len(pts)]

    twin = {}
    for r in doors:
        land = pts[r['ep']]
        here = pts[r['poi']]
        cands = [x for x in doors if x is not r
                 and dist(pts[x['poi']], land) < PAIR_EPS and dist(pts[x['ep']], here) < PAIR_EPS]
        if len(cands) == 1:
            twin[r['idx']] = cands[0]['idx']

    by_idx = {r['idx']: r for r in doors}
    free = {}
    for r in doors:
        t = twin.get(r['idx'])
        if t is None or twin.get(t) != r['idx'] or gated(r) or gated(by_idx[t]):
            continue
        room = room_of(pts[r['poi']], ROOM_EPS)
        land_room = room_of(pts[by_idx[t]['ep']], ROOM_EPS)  # where you appear coming in through r
        if room is None or land_room != room:
            continue
        bgm = r['bgm']
        bgm = int(bgm.split()[0])
        if r['rail'] is None:
            continue
        free[r['idx']] = dict(row=r, twin=t, room=room, bgm=bgm)

    # A map whose doors can't be remixed still gets its pickups remixed (the two
    # modes are independent), so a door problem empties the door table rather
    # than dropping the map.
    free = {k: v for k, v in free.items() if v['twin'] in free}
    door_reason = None
    if len(free) < MIN_FREE_DOORS:
        door_reason = '%d free doors' % len(free)
    elif any(free[v['twin']]['room'] == v['room'] for v in free.values()):
        door_reason = 'camera clustering merges a vanilla door pair into one room'
    if door_reason:
        free = {}

    # Compact room ids and zones (rooms joined by free pairs).
    room_ids = {}
    for k in sorted(free):
        room_ids.setdefault(free[k]['room'], len(room_ids))
    parent = list(range(len(room_ids)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for k, v in free.items():
        parent[find(room_ids[v['room']])] = find(room_ids[free[v['twin']]['room']])
    zone_ids = {}
    for r in range(len(room_ids)):
        zone_ids.setdefault(find(r), len(zone_ids))

    order = sorted(free)
    pos = {k: i for i, k in enumerate(order)}
    door_out = []
    for k in order:
        v = free[k]
        r = v['row']
        door_out.append(dict(event=k, poi=r['poi'], room=room_ids[v['room']],
                             zone=zone_ids[find(room_ids[v['room']])], twin=pos[v['twin']],
                             ep=r['ep'], bgm=v['bgm'], rail=r['rail']))

    # ---- pickups and spots
    door_pois = {r['poi'] for r in rows
                 if r['ss'] in ('SysState_LoadRoom', 'SysState_LoadOverlay') and r['poi'] is not None
                 and r['tt'] != 'TriggerType_None' and r['poi'] < len(pts)}

    draws = pickup_draws(m)
    models = {k: v[0] for k, v in draws.items()}
    pickups = []
    for r in rows:
        tok = (r['dis_tok'] or '').strip()
        if tok not in models or r['ss'] != 'SysState_EventCallback':
            continue
        if r['tt'] != 'TriggerType_TouchFacing' or r['req']:
            continue
        if r['poi'] is None or r['poi'] >= len(pts) or len(by_poi[r['poi']]) != 1:
            continue
        # Outside every camera volume (the open street) is still where the
        # game itself puts the item, so it's a place the player can reach.
        pickups.append(dict(event=r['idx'], poi=r['poi'], flag=r['dis'], flag_tok=tok, model=models[tok],
                            pose=draws[tok][1], pose_idx=draws[tok][2]))

    # Spots are the pickups' own authored places, one per pickup and in pickup
    # order (spot i is where pickup i lies in vanilla): items trade places and
    # each is drawn with the pose of the item that was there -- on the shelf,
    # at the turn the designers gave it -- so it fits the room.
    pickups = pickups[:MAX_SPOTS]
    spots = [(pts[p['poi']][0], pts[p['poi']][1],
              room_of(pts[p['poi']], SPOT_ROOM_EPS) if room_of(pts[p['poi']], SPOT_ROOM_EPS) is not None
              else ('street', i)) for i, p in enumerate(pickups)]
    # Fewer than two spots means nothing can move.
    if len(spots) < 2:
        pickups, spots = [], []
    spot_rooms = {}
    spots = [(x, z, spot_rooms.setdefault(rm, len(spot_rooms))) for x, z, rm in spots]

    # ---- key items: they only ever move within a zone (rooms joined by free doors,
    # or a room behind a single gated door), which is what keeps them reachable
    keys, slots = [], []
    labels = {}
    for v in free.values():
        labels.setdefault(v['room'], set()).add(zone_ids[find(room_ids[v['room']])])
    for r in rows:
        if (r['ss'] in ('SysState_LoadRoom', 'SysState_LoadOverlay') and r['idx'] not in free
                and r['poi'] is not None and r['poi'] < len(pts)):
            b = room_of(pts[r['poi']], ROOM_EPS)
            if b is not None:
                labels.setdefault(b, set()).add('fixed%d' % r['idx'])
    box_zone = box_zones(room_of.boxes, labels)
    hdr = os.path.join(REPO, 'src', 'maps', m, m + '_header.c')
    fm = re.search(r'g_MapEventFuncs\[\][^=]*=\s*\{(.*?)\n\};', rd.strip_comments(open(hdr).read()), re.S) \
        if os.path.exists(hdr) else None
    flist = [e.strip() for e in fm.group(1).split(',')] if fm else []
    kp = key_pickups(m)

    def slot_of(p, any_zone=False):
        box = room_of(p, SPOT_ROOM_EPS)
        if box not in box_zone and not any_zone:
            return None
        for i, sl in enumerate(slots):
            if dist(p, (sl['x'], sl['z'])) < SPOT_DEDUPE:
                return i
        slots.append(dict(x=p[0], z=p[1], zone=box_zone.get(box, NO_ZONE),
                          cx=int(math.floor(p[0] / CELL)), cz=int(math.floor(p[1] / CELL)), common=255,
                          swap_ok=any(dist(p, q) < 2.0 for q in STREET_SWAP_SPOTS.get(m, []))))
        return len(slots) - 1

    by_key = {}
    for r in rows:
        if r['ss'] != 'SysState_EventCallback' or r['ep'] is None or r['ep'] >= len(flist):
            continue
        fn = flist[r['ep']]
        if fn in kp:
            by_key.setdefault(kp[fn][1], []).append(r)
    for flag_tok, krows in by_key.items():
        item, flag, sym, is_pose, sym_idx, take_msg = next(v for v in kp.values() if v[1] == flag_tok)
        # A weapon that may not move within its map (the cafe's knife, one in a
        # room no zone covers) is still listed, pinned: it may only travel
        # with remix_weapons (between areas), never within its map.
        stay = item in STAY_PUT_ITEMS or item in UNLOCKABLE_ITEMS or item == GAS_ITEM
        if stay and item not in WEAPON_ITEMS and item not in UNLOCKABLE_ITEMS and item != GAS_ITEM:
            continue
        pois = sorted({r['poi'] for r in krows})
        optional = item in OPTIONAL_ITEMS
        if len(krows) > 4 or any(p is None or p >= len(pts) for p in pois):
            continue
        # Every other row on those spots must be a message ("already taken",
        # "not unlocked yet", a remark about the spot). The runtime gives the
        # pickup rows their own map points, so those messages stay where they are.
        if any(x['ss'] != 'SysState_ReadMessage' for p in pois for x in by_poi[p] if x not in krows):
            continue
        if any(r['tt'] not in ('TriggerType_TouchFacing', 'TriggerType_TouchObbFacing') for r in krows):
            continue
        if max(dist(pts[a], pts[b]) for a in pois for b in pois) > 2.0:
            continue
        si = slot_of(pts[pois[0]], True)
        # Outside every zone, an item the story needs on a street map trades
        # places with a common pickup lying out on the open street; indoors,
        # with nothing to prove a spot reachable, it stays put.
        swap = (slots[si]['zone'] == NO_ZONE and not optional) or item in STREET_ITEMS.get(m, ())
        if swap and not is_exterior(m):
            if item not in WEAPON_ITEMS and item not in UNLOCKABLE_ITEMS and item != GAS_ITEM:
                continue
            swap, stay = False, True
        keys.append(dict(flag=flag_value(flag_tok), flag_tok=flag_tok, item=item, sym=sym, pose=is_pose,
                         idx=sym_idx, rows=[(r['idx'], r['poi'], r['req']) for r in krows], slot=si,
                         anywhere=optional and not stay, swap=swap and not stay, take_msg=take_msg, pinned=stay,
                         unlock=flag_value(UNLOCK_FLAGS[item]) if item in UNLOCK_FLAGS else 0))
    # Common pickups' spots: where items normally are. Outside a zone they only
    # serve optional items, and only the ones items mode can move off them.
    any_optional = any(k['anywhere'] or k['swap'] for k in keys)
    for ci, p in enumerate(pickups):
        # Where reachability is worked out room by room (REACH_MAPS), a pickup
        # outside every zone is still a spot a key may be shown to reach.
        si = slot_of(pts[p['poi']], any_optional or m in REACH_MAPS)
        if si is not None and slots[si]['common'] == 255:
            slots[si]['common'] = ci

    # Keys go only to item spots: other keys' and common pickups' own places
    # (each drawn with that item's pose), never to a bare spot on the floor.
    if not keys:
        slots = []

    zone_sizes = {}
    for d in door_out:
        zone_sizes[d['zone']] = zone_sizes.get(d['zone'], 0) + 1
    if door_out and max(zone_sizes.values()) <= 2:
        door_reason = 'no zone with alternatives'
        door_out = []
    if not door_out and not pickups and not keys:
        return dict(reason=door_reason or 'nothing to remix')

    def zone_of(p):
        box = room_of(p, SPOT_ROOM_EPS)
        return box_zone.get(box, NO_ZONE) if box is not None else NO_ZONE

    return dict(doors=door_out, rooms=len(room_ids) if door_out else 0,
                zones=len(zone_ids) if door_out else 0, door_reason=door_reason,
                pickups=pickups, spots=spots, point_count=len(pts), keys=keys, slots=slots,
                graph=dict(rows=rows, pts=pts, gated=gated, zone_of=zone_of, free=set(free)))


# ---- what is reachable when (keys moving past their own zone)
#
# The whole game's interiors as one directed graph. A node is a room: a group of
# touching camera boxes in one map (the boxes a key slot or a doorway lies in).
# Every SysState_LoadRoom / SysState_LoadOverlay row is an edge from the room of
# its doorway to the room of its arrival point, labelled with what it takes:
#   ('flag', F)     its requiredEventFlag
#   ('item', X)     its requiredItemId / Item activation
#   ('not', F)      a disabledEventFlag that gates it (it works only while F is clear)
#   ('row', m, i)   anything we can't reason about (automatic rows, a doorway
#                   shared with other rows that may answer first)
# A key K's room R is reached at the moment K is picked up. Every flag that
# every route from an entry into the map set to R needs is set by then (flags
# only get set). With exactly those open, the rooms R can walk to AND walk back
# from are reachable while K lies there, so K may go to any item spot in them.
# Edges we can't reason about only ever count as closed, and entries are every
# arrival point any other map's row leads to, so both errors make the set smaller.

REACH_MAPS = {'map1_s00', 'map1_s01', 'map1_s02', 'map1_s03', 'map3_s03', 'map3_s04', 'map3_s05',
              'map7_s01', 'map7_s02'}
# Maps whose keys may also trade places with a pickup of another map of the same
# group (one building, its floors joined by ungated stairs). Nowhere's two maps
# are the same rooms at two points of the story, sharing their items' flags, so
# its keys only move within their map.
XMAP_GROUPS = [('map1_s00', 'map1_s01'), ('map1_s02', 'map1_s03'), ('map3_s03', 'map3_s04', 'map3_s05')]

# Parts of a room island that lie behind a locked door of their own: the door's
# two sides sit 0.05 apart inside one camera box, so the island joins them. Each
# rectangle (x0, x1, z0, z1) is a node of its own.
ISLAND_SPLITS = {
    # The Otherworld school's east stairwell, behind the door the Teacher's Room
    # phone scene unlocks (flag 1201): the stairs up, the stairs down. Until then
    # the second floor is reached only through the Picture Card's door.
    'map1_s02': [(104.0, 118.0, 38.0, 47.52)],
}
ISLAND_SPLIT_BASE = 100000

# Transitions the event table shows open that the game keeps shut until later,
# by means the table doesn't show. Counted closed: a key never relies on them.
# {(map, row index)}
CLOSED_ROWS = {
    # The Otherworld hospital's stairs between 1F and the basement: the basement
    # is first reached by the elevator from 2F east, past the four-plate door.
    ('map3_s04', 41), ('map3_s05', 25),
}

_GRAPH = {}
# A room is an island of camera boxes: boxes of one room lie at most ~4 units
# apart, rooms 11+ (the doors between them are teleports).
ROOM_ISLAND_GAP = 6.0
DOOR_NODE_EPS = 5.0  # a stairwell's trigger box sits up to ~3.2 units past its camera box; rooms are 20+ apart


def map_graph(m):
    if m in _GRAPH:
        return _GRAPH[m]
    out = None
    try:
        pts = [(q(rd.field(p, 'positionX')), q(rd.field(p, 'positionZ'))) for p in rd.map_points(m)]
        blocks = map_events(m)
    except Exception:
        pts, blocks = [], []
    if pts and blocks:
        room_of = camera_rooms(m)
        boxes = room_of.boxes
        parent = list(range(len(boxes)))

        def find(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        for i in range(len(boxes)):
            a = boxes[i]
            for j in range(i):
                b = boxes[j]
                if (a[0] <= b[1] + ROOM_ISLAND_GAP and b[0] <= a[1] + ROOM_ISLAND_GAP and
                        a[2] <= b[3] + ROOM_ISLAND_GAP and b[2] <= a[3] + ROOM_ISLAND_GAP):
                    parent[find(i)] = find(j)

        def node(p, eps=SPOT_ROOM_EPS):
            b = room_of(p, eps)
            if b is None:
                return None
            for k, (x0, x1, z0, z1) in enumerate(ISLAND_SPLITS.get(m, ())):
                if x0 <= p[0] <= x1 and z0 <= p[1] <= z1:
                    return (m, ISLAND_SPLIT_BASE + 100 * k + find(b))
            return (m, find(b))

        rows = []
        for i, e in enumerate(blocks):
            tt = rd.field(e, 'triggerType')
            if tt == 'TriggerType_EndOfArray':
                break
            mi = re.search(r'\.mapIdx\s*=\s*MapIdx_(MAP\d_S\d\d)', e)
            rows.append(dict(idx=i, tt=tt, at=rd.field(e, 'activationType'), ss=rd.field(e, 'sysState'),
                             poi=int_field(e, 'pointOfInterestIdx'), ep=int_field(e, 'eventParam'),
                             req=flag_value(rd.field(e, 'requiredEventFlag')),
                             dis=flag_value(rd.field(e, 'disabledEventFlag')),
                             item=rd.field(e, 'requiredItemId'),
                             target=mi.group(1).lower() if mi else None))
        out = dict(pts=pts, rows=rows, node=node)
    _GRAPH[m] = out
    return out


def game_graph(skip=None):
    """[(src node, dst node, frozenset(conditions))], entry nodes. Rows in
    `skip` ((map, row index)) are left out."""
    edges, entries = [], set()
    for m in rd.MAP_NAMES:
        g = map_graph(m)
        if not g:
            continue
        pts, rows = g['pts'], g['rows']
        by_poi = {}
        for r in rows:
            by_poi.setdefault(r['poi'], []).append(r)
        for r in rows:
            if r['ss'] not in ('SysState_LoadRoom', 'SysState_LoadOverlay') or r['poi'] is None or r['ep'] is None:
                continue
            if skip and (m, r['idx']) in skip:
                continue
            if r['poi'] >= len(pts):
                continue
            if r['ss'] == 'SysState_LoadOverlay':
                # The arrival point is one of this map's points, in the
                # target map's coordinates (SysState_LoadOverlay copies it out
                # before the overlay goes).
                tm = r['target']
                tg = map_graph(tm) if tm else None
                if not tg or r['ep'] >= len(pts):
                    continue
                dst = tg['node'](pts[r['ep']], DOOR_NODE_EPS)
                if dst is not None and tm != m:
                    entries.add(dst)
            else:
                if r['ep'] >= len(pts):
                    continue
                dst = g['node'](pts[r['ep']], DOOR_NODE_EPS)
            # Where a scripted transition (no trigger of its own) puts the
            # player counts as an entry too: we can't tell what led there.
            if dst is not None and r['tt'] == 'TriggerType_None':
                entries.add(dst)
            src = g['node'](pts[r['poi']], DOOR_NODE_EPS)
            if src is None or dst is None:
                continue
            c = set()
            if r['req']:
                c.add(('flag', r['req']))
            if (r['item'] not in (None, '0', 'InvItemId_Unequipped')) or r['at'] == 'TriggerActivationType_Item':
                c.add(('item', r['item']))
            if r['dis'] and (r['dis'] < DISABLED_FLAG_MARK_MIN or r['at'] == 'TriggerActivationType_Exclusive'):
                c.add(('not', r['dis']))
            if r['tt'] == 'TriggerType_None' or len(by_poi[r['poi']]) > 1 or (m, r['idx']) in CLOSED_ROWS:
                c.add(('row', m, r['idx']))
            edges.append((src, dst, frozenset(c)))
    return edges, entries


def _reach(starts, edges, ok):
    seen, todo = set(starts), list(starts)
    adj = {}
    for a, b, c in edges:
        if ok(c):
            adj.setdefault(a, []).append(b)
    while todo:
        n = todo.pop()
        for b in adj.get(n, ()):
            if b not in seen:
                seen.add(b)
                todo.append(b)
    return seen


_REACH = {}


def reach_set(node):
    """Rooms reachable from `node` and back while what reaching `node` needs is
    all the player is sure to have. None: nothing provable beyond its zone."""
    if node in _REACH:
        return _REACH[node]
    if '_G' not in _REACH:
        _REACH['_G'] = game_graph()
    edges, entries = _REACH['_G']
    res = None
    if entries and node in _reach(entries, edges, lambda c: True):
        gates = {x for _, _, c in edges for x in c if x[0] == 'flag'}
        need = {gt for gt in gates if node not in _reach(entries, edges, lambda c, gt=gt: gt not in c)}
        ok = lambda c: c <= need
        fwd = _reach([node], edges, ok)
        rev_edges = [(b, a, c) for a, b, c in edges]
        back = _reach([node], rev_edges, ok)
        res = fwd & back
    _REACH[node] = res
    return res


# ---- rooms across floors (pc_remix.c, "Rooms across floors")
#
# A building's floors are separate maps joined by stairs that are always open
# both ways. The zones the stairs join -- through any doors that are always open
# both ways, rewirable or not -- are one group: their free doors are rewired
# together, so a door may lead to a room on another floor. The rooms the stairs
# join stay joined, as one node of the rebuild, so the group stays connected.


def door_groups(results):
    # The stairs' rooms are found without the free doors: those are the ones
    # being rewired, so a room reached through one is not joined to the stairs
    # in the new layout. (Walking through them once merged the fog school
    # lobby's one-door box with the stairs, and its door could then lead into
    # a bathroom: a dead end.)
    free_rows = {(m, d['event']) for m, r in results.items() for d in (r['doors'] or ())}
    edges, _ = game_graph(skip=free_rows)
    open_e = {(a, b) for a, b, c in edges if not c}
    mutual = {}
    for a, b in open_e:
        if (b, a) in open_e:
            mutual.setdefault(a, set()).add(b)
    door_rooms = {}   # island -> {(map, zone, room)}
    for m, r in results.items():
        if not r['doors']:
            continue
        mg = map_graph(m)
        for d in r['doors']:
            n = mg['node'](mg['pts'][d['poi']], DOOR_NODE_EPS)
            door_rooms.setdefault(n, set()).add((m, d['zone'], d['room']))

    def side(n):
        """The free-door rooms joined to island n within its map by walking and
        fixed doors open both ways: every one of them, since each is really
        joined to the stairs."""
        seen, todo, found = {n}, [n], set()
        while todo:
            x = todo.pop()
            found.update(door_rooms.get(x, ()))
            for y in mutual.get(x, ()):
                if y[0] == n[0] and y not in seen:
                    seen.add(y)
                    todo.append(y)
        return sorted(found)

    parent = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    merges = []
    # Sorted: set iteration order varies run to run, and the merge order is
    # part of the generated table (so of the layout a seed gives).
    for a, nbrs in sorted(mutual.items()):
        for b in sorted(nbrs):
            if a[0] == b[0] or a[0] not in results or b[0] not in results or a > b:
                continue
            ra, rb = side(a), side(b)
            if not ra or not rb:
                continue
            rooms = ra + rb
            merges.append(rooms)
            for t in rooms[1:]:
                parent[find((t[0], t[1]))] = find((rooms[0][0], rooms[0][1]))
    groups = {}
    for rooms in merges:
        groups.setdefault(find((rooms[0][0], rooms[0][1])), []).append(rooms)
    out = []
    for root, ms in sorted(groups.items()):
        members = sorted({(t[0], t[1]) for rooms in ms for t in rooms})
        if len({mm for mm, _ in members}) < 2:
            continue
        out.append(dict(members=members, merges=ms))
    return out


# ---- items moving between areas (pc_remix.c, "Between areas")
#
# A rule lets each of its `a` items, by chance (1 in `chance`, per seed), trade
# places with one of the common pickups lying at its `b_spots` in another area:
# the key goes to the pickup's spot there, the pickup to the key's spot here.
XMAP_RULES = [
    # The Old Silent Hill keys may turn up in the convenience store (one of its
    # four health/ammo spots), and that pickup out on the key's own spot.
    dict(a_map='map2_s00', a_items=['InvItemId_KeyOfLion', 'InvItemId_KeyOfWoodman', 'InvItemId_KeyOfScarecrow'],
         b_map='map0_s02', b_spots=[(-147.1, 22.2), (-141.7, 21.0), (-141.0, 19.0), (-137.8, 18.6)], chance=3),
    # The Channeling Stone may turn up in Old Silent Hill before the Levin St
    # house's door: on the street, in the house, in the Key of Woodman's lane.
    # Or on the steel pipe's spot, the pipe then lying where the Stone was (the
    # convenience store, joined to the street by ungated doors both ways).
    dict(a_map='map0_s02', a_items=['InvItemId_ChannelingStone'],
         b_map='map2_s00', b_spots=STREET_SWAP_SPOTS['map2_s00'], b_keys=['InvItemId_SteelPipe'], chance=2),
]


def map_messages(m):
    """MAP_MESSAGES entries as C literal source, index = message number."""
    src = rd.strip_comments(''.join(open(f, errors='ignore').read()
                                    for f in glob.glob(os.path.join(REPO, 'src', 'maps', m, '*.c'))))
    mm = re.search(r'MAP_MESSAGES\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not mm:
        return []
    body = mm.group(1)
    out = [None] * 15  # maps/shared/map_msg_common.h
    body = body.split('map_msg_common.h"', 1)[1] if 'map_msg_common.h"' in body else body
    cur, i = [], 0
    while i < len(body):
        c = body[i]
        if c == '"':
            j = i + 1
            while body[j] != '"':
                j += 2 if body[j] == '\\' else 1
            cur.append(body[i:j + 1])
            i = j + 1
        elif c == ',':
            if cur:
                out.append(' '.join(cur))
            cur = []
            i += 1
        else:
            i += 1
    if cur:
        out.append(' '.join(cur))
    return out


def key_pose(m, sym, idx):
    """(model name, (x, y, z), (rx, ry, rz) degrees) of a key's world object."""
    src = rd.strip_comments(''.join(open(f, errors='ignore').read()
                                    for f in glob.glob(os.path.join(REPO, 'src', 'maps', m, '*.c'))))
    target = r'&%s\[%d\]' % (re.escape(sym), idx) if idx else r'&%s(?:\[0\])?' % re.escape(sym)
    num = r'\s*(-?[\d.]+)f?\s*'
    mm = re.search(r'\b(?:WorldObjectInit|WorldObject_Init)\(\s*' + target + r'\s*,\s*"([^"]+)"\s*,' +
                   ','.join([num] * 6) + r'\)', src)
    if mm:
        v = [float(x) for x in mm.groups()[1:]]
        return mm.group(1), tuple(v[:3]), tuple(v[3:])
    mm = re.search(r'\b(?:WorldObject_PosePositionInit|WorldObject_PlacementInit|WorldObjectNoRotInit)\(\s*' + target +
                   r'\s*,\s*"([^"]+)"\s*,' + ','.join([num] * 3) + r'\)', src)
    if mm:
        v = [float(x) for x in mm.groups()[1:]]
        return mm.group(1), tuple(v), (0.0, 0.0, 0.0)
    return None


def map_tag(m):
    hdr = os.path.join(REPO, 'src', 'maps', m, m + '_header.c')
    mm = re.search(r'\.mapInfo\s*=\s*&MAP_INFOS\[MapType_(\w+)\]', open(hdr).read())
    t = mm.group(1)
    src = open(os.path.join(REPO, 'src', 'bodyprog', 'sys', 'map_info.c')).read()
    rows = re.search(r'MAP_INFOS\[MapType_Count\]\s*=\s*\{(.*?)\n\};', src, re.S).group(1)
    enum = open(os.path.join(REPO, 'include', 'bodyprog', 'map', 'map.h')).read()
    names = re.findall(r'MapType_(\w+)\s*[,=]', re.search(r'enum _MapType\s*\{(.*?)\}', enum, re.S).group(1))
    ents = re.findall(r'\{\s*([\w-]+)\s*,\s*"(\w*)"', rows)
    plm, tag = ents[names.index(t)]
    return tag, (plm if plm != 'NO_VALUE' else '-1')


def _xmap_key_item(m, r, ki, k):
    """A key as a between-areas item, or None if it can't be one (no take message
    to show elsewhere, or no pose to draw its model with)."""
    msgs = map_messages(m)
    if k['take_msg'] is None or k['take_msg'] >= len(msgs):
        return None
    pose = key_pose(m, k['sym'], k['idx'])
    if pose is None:
        return None
    tag, plm = map_tag(m)
    sl = r['slots'][k['slot']]
    return dict(map=m, key=True, local=ki, item=k['item'], flag=k['flag'], req=k['rows'][0][2],
                x=sl['x'], z=sl['z'], pose=pose, msg=msgs[k['take_msg']], tag=tag, plm=plm,
                count=XMAP_TAKE_COUNT.get(k['item'], 'DEFAULT_PICKUP_ITEM_COUNT'),
                cx=int(math.floor(pose[1][0] / CELL)), cz=int(math.floor(pose[1][2] / CELL)))


def _xmap_pickup_item(m, r, ci):
    pk = r['pickups'][ci]
    pts = map_graph(m)['pts']
    ppos = pts[pk['poi']]
    return dict(map=m, key=False, local=ci, model=pk['model'], flag=pk['flag'], x=ppos[0], z=ppos[1])


# Event_ItemTake counts other than one (the Shotgun comes loaded).
XMAP_TAKE_COUNT = {'InvItemId_Shotgun': 'SHOTGUN_AMMO_PICKUP_ITEM_COUNT'}


def key_node(m, x, z):
    return map_graph(m)['node']((x, z), DOOR_NODE_EPS)


def key_reach(m, r, k):
    """Bitmask of r's slots key k may go to beyond its zone's: those in a room
    reach_set proves reachable (and back) while k lies in its own."""
    if m not in REACH_MAPS:
        return 0, None
    home = r['slots'][k['slot']]
    S = reach_set(key_node(m, home['x'], home['z']))
    if not S:
        return 0, None
    # A key used by walking in with it (the Blood Pack, dropped for the tentacles
    # of room 204) never lies in that room: taking it there is what it guards.
    mg = map_graph(m)
    hazard = {mg['node'](mg['pts'][x['poi']], DOOR_NODE_EPS) for x in mg['rows']
              if x['item'] == k['item'] and x['tt'] == 'TriggerType_TouchAabb' and x['poi'] is not None and
              x['poi'] < len(mg['pts'])}
    S = S - hazard
    mask = 0
    for j, sl in enumerate(r['slots']):
        if j < 64 and key_node(m, sl['x'], sl['z']) in S:
            mask |= 1 << j
    return mask, S


def xmap_items(results):
    """Between-areas rules: [(rule, a items, b items)]. A rule's a items each
    trade, with chance num/den, with one of its b items."""
    out = []
    for rule in XMAP_RULES:
        ra, rb = results.get(rule['a_map']), results.get(rule['b_map'])
        if not ra or not rb:
            continue
        # Unlockables travel by their own rule (remix_unlockables), not with the key items.
        a_items = [it for it in (_xmap_key_item(rule['a_map'], ra, ki, k) for ki, k in enumerate(ra['keys'])
                                 if k['item'] in rule['a_items'] and k['item'] not in UNLOCKABLE_ITEMS) if it]
        pts = map_graph(rule['b_map'])['pts']
        b_items = [_xmap_pickup_item(rule['b_map'], rb, ci) for ci, pk in enumerate(rb['pickups'])
                   if any(dist(pts[pk['poi']], sp) < 1.0 for sp in rule['b_spots'])]
        b_items += [it for it in (_xmap_key_item(rule['b_map'], rb, ki, k) for ki, k in enumerate(rb['keys'])
                                  if k['item'] in rule.get('b_keys', ())) if it]
        if a_items and b_items:
            out.append((dict(num=1, den=rule['chance']), a_items, b_items))
            print('  between areas: %s %s may trade with one of %d items in %s' % (
                rule['a_map'], ', '.join(i['item'][10:] for i in a_items), len(b_items), rule['b_map']))
    # One building's floors: a key may trade with a pickup on another floor that
    # is reachable (and back) while the key lies where it does. Every spot it may
    # take, on its own floor or another, is equally likely.
    for group in XMAP_GROUPS:
        for ma in group:
            ra = results.get(ma)
            if not ra:
                continue
            for ki, k in enumerate(ra['keys']):
                if k['swap'] or k['anywhere'] or k['unlock']:
                    continue
                mask, S = key_reach(ma, ra, k)
                if not S:
                    continue
                home = ra['slots'][k['slot']]
                n_in = sum(1 for j, sl in enumerate(ra['slots'])
                           if (sl['zone'] == home['zone'] and home['zone'] != NO_ZONE) or (mask >> j) & 1)
                per = []
                for mb in group:
                    rb = results.get(mb)
                    if mb == ma or not rb or not rb['pickups']:
                        continue
                    gb = map_graph(mb)
                    c = [ci for ci, pk in enumerate(rb['pickups'])
                         if gb['node'](gb['pts'][pk['poi']], DOOR_NODE_EPS) in S]
                    if c:
                        per.append((mb, c))
                a_it = _xmap_key_item(ma, ra, ki, k)
                if not per or not a_it:
                    continue
                left = sum(len(c) for _, c in per)
                for mb, c in per:
                    rb = results[mb]
                    out.append((dict(num=len(c), den=max(n_in, 1) + left), [a_it],
                                [_xmap_pickup_item(mb, rb, ci) for ci in c]))
                    left -= len(c)
                print('  between areas: %s %s may also lie on %s' % (
                    ma, k['item'][10:], ', '.join('%d spot(s) in %s' % (len(c), mb) for mb, c in per)))

    # Weapons (remix_weapons): each always leaves home, for one of every health
    # or ammo spot in the game (one shared list), or for a key's spot when that
    # key may take the weapon's place (its reach_set holds the weapon's room).
    commons = []
    for mb, rb in results.items():
        for ci in range(len(rb['pickups'])):
            commons.append(_xmap_pickup_item(mb, rb, ci))
    # The Gasoline Tank's spot, free only while the tank is out of the game
    # (pc_remix.c gas_removed); nothing goes back to the mover's spot then.
    for mg, rg in results.items():
        for ki, k in enumerate(rg['keys']):
            if k['item'] == GAS_ITEM:
                it = _xmap_key_item(mg, rg, ki, k)
                if it:
                    commons.append(it)
    reach_keys = []
    for mk, rk in results.items():
        if mk not in REACH_MAPS:
            continue
        for ki, k in enumerate(rk['keys']):
            if k['item'] in WEAPON_ITEMS or k.get('pinned') or k['swap'] or k['anywhere'] or k['unlock']:
                continue
            _, S = key_reach(mk, rk, k)
            it = _xmap_key_item(mk, rk, ki, k) if S else None
            if it:
                reach_keys.append((it, S))
    nw = 0
    for mw, rw in results.items():
        for ki, k in enumerate(rw['keys']):
            # STAY_PUT_ITEMS stay: the cafe's knife (the radio cutscene that
            # unlocks the cafe's door waits for its flag and the map's), the
            # Chainsaw and Rock Drill (drawn only with the Gasoline Tank).
            if k['item'] not in WEAPON_ITEMS or k['item'] in STAY_PUT_ITEMS or k['item'] in UNLOCKABLE_ITEMS:
                continue
            a_it = _xmap_key_item(mw, rw, ki, k)
            if not a_it:
                continue
            if not a_it['req'] and k['unlock']:
                a_it['req'] = k['unlock']   # a bonus weapon shows, and is taken, only once unlocked
            a_list = [a_it]
            home = rw['slots'][k['slot']]
            wnode = key_node(mw, home['x'], home['z'])
            kb = [it for it, S in reach_keys if wnode in S and it['flag'] != a_it['flag']]
            if kb:
                out.append((dict(num=len(kb), den=len(kb) + len(commons), weapons=1), a_list, kb))
            out.append((dict(num=1, den=1, weapons=1), a_list, commons))
            nw += 1
            print('  weapons: %s %s may lie on any of %d health/ammo spots%s' % (
                mw, k['item'][10:], len(commons), (' or %d key spot(s)' % len(kb)) if kb else ''))
    for mu, ru in results.items():
        for ki, k in enumerate(ru['keys']):
            if k['item'] not in UNLOCKABLE_ITEMS:
                continue
            a_it = _xmap_key_item(mu, ru, ki, k)
            if not a_it:
                print('  unlockables: %s %s has no take message or pose -- stays' % (mu, k['item'][10:]))
                continue
            # Shown and taken once unlocked; the fuel the Chainsaw and Rock Drill
            # wait for is no unlock.
            a_it['req'] = 0 if k['item'] in FUEL_ITEMS else (a_it['req'] or k['unlock'])
            out.append((dict(num=1, den=1, weapons=2), [a_it], commons))
            print('  unlockables: %s %s may lie on any of %d health/ammo spots' % (mu, k['item'][10:], len(commons)))
    return out


def main():
    enum = mapidx_enum()
    for name, idx in rd.IDX.items():
        if enum.get(name) != idx:
            sys.exit("map index mismatch for %s: enum %s, generator %d" % (name, enum.get(name), idx))

    results = {}
    for m in rd.MAP_NAMES:
        r = analyze(m)
        if r is None:
            continue
        if 'reason' in r:
            if r['reason'] not in ('0 free doors', 'nothing to remix'):
                print('  skip %-9s %s' % (m, r['reason']))
            continue
        results[m] = r
        print('  %-9s %2d free doors, %2d rooms, %2d zones, %d pickups / %d spots, %d keys / %d slots%s' % (
            m, len(r['doors']), r['rooms'], r['zones'], len(r['pickups']), len(r['spots']),
            len(r['keys']), len(r['slots']),
            '  (items only: %s)' % r['door_reason'] if not r['doors'] else ''))

    for m, r in results.items():
        keep = []
        for k in r['keys']:
            if k['swap']:
                n = sum(1 for sl in r['slots'] if sl['swap_ok'] and sl['common'] != 255)
                if n == 0:
                    continue
                print('  %-9s %s: trades places with one of %d pickups reachable before it is used' % (m, k['item'], n))
            keep.append(k)
        r['keys'] = keep
        if not keep:
            r['slots'] = []
    for m in [m for m, r in results.items() if not r['doors'] and not r['pickups'] and not r['keys']]:
        del results[m]

    L = []
    w = L.append
    w('/* AUTO-GENERATED by pc_port/tools/gen_remix_data.py -- do not edit. */')
    w('/* SPDX-License-Identifier: GPL-3.0-or-later */')
    w('#ifndef PC_REMIX_DATA_H')
    w('#define PC_REMIX_DATA_H')
    w('')
    w('#include "pc_remix_layout.h"')
    w('')
    for m, r in results.items():
        u = m.upper()
        if r['doors']:
            w('static const s_RemixDoor REMIX_DOORS_%s[] = {' % u)
            w('    /* event, poi, room, zone, twin, ep, bgm, rail */')
            for d in r['doors']:
                w('    { %3d, %3d, %2d, %2d, %2d, %3d, %2d, %d },' % (
                    d['event'], d['poi'], d['room'], d['zone'], d['twin'], d['ep'], d['bgm'], d['rail']))
            w('};')
        if r['pickups']:
            w('static const s_RemixPickup REMIX_PICKUPS_%s[] = {' % u)
            w('    /* event, poi, model, flag, pose symbol, pose index */')
            for p in r['pickups']:
                pose = '"%s"' % p['pose'] if p['pose'] else 'NULL'
                w('    { %3d, %3d, %d, %4d, %s, %d }, /* %s */' % (p['event'], p['poi'], p['model'], p['flag'],
                                                            pose, p['pose_idx'], p['flag_tok']))
            w('};')
            w('static const s_RemixSpot REMIX_SPOTS_%s[] = {' % u)
            w('    /* x, z (q19_12), room */')
            for x, z, rm in r['spots']:
                w('    { %8d, %8d, %2d }, /* (%.1f, %.1f) */' % (q12(x), q12(z), rm, x, z))
            w('};')
        if r['keys']:
            w('static const s_RemixKey REMIX_KEYS_%s[] = {' % u)
            w('    /* model symbol, is pose, flag, slot, rows { event, poi, requiredEventFlag }, anywhere,'
              ' symbol array index, swap, unlock flag (0 = none), pinned, slots reachable beyond its zone, item */')
            for k in r['keys']:
                rows_c = ', '.join('{ %d, %d, %d }' % (e, poi, req) for e, poi, req in k['rows'])
                mask, _ = key_reach(m, r, k)
                w('    { "%s", %d, %4d, %2d, %d, { %s }, %d, %d, %d, %d, %d, 0x%XULL, %d }, /* %s */' % (
                    k['sym'], 1 if k['pose'] else 0, k['flag'], k['slot'], len(k['rows']), rows_c,
                    1 if k['anywhere'] else 0, k['idx'], 1 if k['swap'] else 0, k['unlock'],
                    1 if k.get('pinned') else 0, 0 if k.get('pinned') else mask, item_id(k['item']), k['item']))
            w('};')
            w('static const s_RemixSlot REMIX_KEYSLOTS_%s[] = {' % u)
            w('    /* x, z (q19_12), zone, cell x, cell z, common pickup there (255 = none), street key may trade here */')
            for sl in r['slots']:
                w('    { %8d, %8d, %3d, %3d, %3d, %3d, %d }, /* (%.1f, %.1f) */' % (
                    q12(sl['x']), q12(sl['z']), sl['zone'], sl['cx'], sl['cz'], sl['common'],
                    1 if sl['swap_ok'] else 0, sl['x'], sl['z']))
            w('};')
        w('')

    xm = xmap_items(results)
    w('/* Items that may trade places between areas (see XMAP_RULES in the generator).')
    w(' * Uses the game\'s item and file enums: only there when game.h came first. */')
    w('#ifdef Q12')
    w('static const s_RemixXItem REMIX_X_ITEMS[] = {')
    w('    /* map, is key, local index, common type, item, flag, required flag, spot x, z,')
    w('       pose x, y, z, rotation x, y, z, take message, model, IPD tag, cell x, z, global model set file,')
    w('       take count */')
    nx = 0
    rules_c = []
    placed = {}

    def emit(items):
        nonlocal nx
        if id(items) in placed:
            return placed[id(items)]
        first = nx
        for it in items:
            if it['key']:
                (name, (px, py, pz), (rx, ry, rz)) = it['pose']
                w('    { %d, 1, %d, 0, %s, %d, %d, %d, %d, Q12(%sf), Q12(%sf), Q12(%sf), Q12_ANGLE(%sf), Q12_ANGLE(%sf), '
                  'Q12_ANGLE(%sf), %s, "%s", "%s", %d, %d, %s, %s }, /* %s */' % (
                      rd.IDX[it['map']], it['local'], it['item'], it['flag'], it['req'] or 0,
                      q12(it['x']), q12(it['z']), repr(px), repr(py), repr(pz), repr(rx), repr(ry), repr(rz),
                      it['msg'], name, it['tag'], it['cx'], it['cz'], it['plm'], it['count'], it['map']))
            else:
                w('    { %d, 0, %d, %d, 0, %d, 0, %d, %d, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, 0, 0, -1, 0 }, '
                  '/* %s common at (%.1f, %.1f) */' % (
                      rd.IDX[it['map']], it['local'], it['model'], it['flag'], q12(it['x']), q12(it['z']),
                      it['map'], it['x'], it['z']))
            nx += 1
        placed[id(items)] = (first, len(items))
        return placed[id(items)]

    for rule, a_items, b_items in xm:
        fa, na = emit(a_items)
        fb, nb = emit(b_items)
        rules_c.append('    { %d, %d, %d, %d, %d, %d, %d },' % (fa, na, fb, nb, rule['num'], rule['den'],
                                                              rule.get('weapons', 0)))
    if nx == 0:
        w('    { 0 }')
    w('};')
    w('static const s_RemixXRule REMIX_X_RULES[] = {')
    w('    /* first a item, a count, first b item, b count, chance num / den (per a item), weapons rule */')
    for line in rules_c:
        w(line)
    if not rules_c:
        w('    { 0 }')
    w('};')
    w('#define REMIX_X_RULE_COUNT %d' % len(rules_c))
    w('#endif')
    w('')

    # Every item the remix can move, by the name remix_fixed_items uses.
    names = set()
    for m, r in results.items():
        for k in r['keys']:
            if k['item'] not in STAY_PUT_ITEMS and k['item'] != GAS_ITEM:
                names.add(k['item'])
    w('/* Items remix_fixed_items may name (InvItemId without its prefix): 0 key item, 1 weapon, 2 unlockable. */')
    w('static const s_RemixItemName REMIX_ITEM_NAMES[] = {')
    for it in sorted(names, key=item_id):
        kind = 2 if it in UNLOCKABLE_ITEMS else 1 if it in WEAPON_ITEMS else 0
        w('    { %3d, %d, "%s" },' % (item_id(it), kind, it[10:]))
    w('};')
    w('')

    w('static const s_RemixMap REMIX_MAPS[REMIX_MAP_COUNT] = {')
    for m, r in results.items():
        u = m.upper()
        if r['pickups']:
            pk = 'REMIX_PICKUPS_%s, %d, REMIX_SPOTS_%s, %d' % (u, len(r['pickups']), u, len(r['spots']))
        else:
            pk = 'NULL, 0, NULL, 0'
        dr = ('REMIX_DOORS_%s' % u) if r['doors'] else 'NULL'
        ky = ('REMIX_KEYS_%s, %d, REMIX_KEYSLOTS_%s, %d' % (u, len(r['keys']), u, len(r['slots']))
              if r['keys'] else 'NULL, 0, NULL, 0')
        w('    [%d] = { %s, %d, %d, %s, %d, %s }, /* MapIdx_%s */' % (
            rd.IDX[m], dr, len(r['doors']), r['zones'], pk, r['point_count'], ky, u))
    w('};')
    w('')

    # Rooms across floors: the groups, and every door's vanilla arrival point (a
    # door on another floor arrives with one of these, copied into this map).
    groups = door_groups(results)
    w('/* Rooms across floors (docs/Remix_Mode.md): zones of a building\'s floors whose')
    w(' * free doors are rewired together. Rooms listed with one set id are one node of')
    w(' * the rebuild: stairs and doors that are always open both ways join them. */')
    mem_c, mrg_c, grp_c = [], [], []
    for gi, gr in enumerate(groups):
        fm, fg = len(mem_c), len(mrg_c)
        for mm, z in gr['members']:
            mem_c.append('    { %d, %d }, /* %s zone %d */' % (rd.IDX[mm], z, mm, z))
        for si, rooms in enumerate(gr['merges']):
            for mm, z, room in rooms:
                mrg_c.append('    { %d, %d, %d }, /* %s room %d */' % (rd.IDX[mm], room, si, mm, room))
        grp_c.append('    { &REMIX_DOOR_GROUP_MEMBERS[%d], %d, &REMIX_DOOR_GROUP_MERGES[%d], %d }, /* %s */' % (
            fm, len(gr['members']), fg, len(mrg_c) - fg, ', '.join(sorted({mm for mm, _ in gr['members']}))))
        print('  rooms across floors: %s' % ', '.join('%s zone %d' % (mm, z) for mm, z in gr['members']))
    w('static const s_RemixGroupMember REMIX_DOOR_GROUP_MEMBERS[] = {')
    for l in mem_c or ['    { 0 }']:
        w(l)
    w('};')
    w('static const s_RemixGroupMerge REMIX_DOOR_GROUP_MERGES[] = {')
    for l in mrg_c or ['    { 0 }']:
        w(l)
    w('};')
    w('static const s_RemixDoorGroup REMIX_DOOR_GROUPS[] = {')
    for l in grp_c or ['    { 0 }']:
        w(l)
    w('};')
    w('#define REMIX_DOOR_GROUP_COUNT %d' % len(grp_c))
    w('')
    in_group = sorted({mm for gr in groups for mm, _ in gr['members']})
    w('#ifdef Q12')
    for mm in in_group:
        blocks = rd.map_points(mm)
        w('static const s_MapPoint2d REMIX_DOOR_ARRIVALS_%s[] = {' % mm.upper())
        for d in results[mm]['doors']:
            w('    %s, /* door row %d */' % (' '.join(blocks[d['ep']].split()), d['event']))
        w('};')
    w('static const s_MapPoint2d* const REMIX_DOOR_ARRIVALS[REMIX_MAP_COUNT] = {')
    for mm in in_group:
        w('    [%d] = REMIX_DOOR_ARRIVALS_%s,' % (rd.IDX[mm], mm.upper()))
    w('};')
    w('#endif')
    w('')
    w('#endif /* PC_REMIX_DATA_H */')
    w('')

    if len(rd.MAP_NAMES) > 48:
        sys.exit("REMIX_MAP_COUNT too small")
    with open(OUT, 'w', newline='\n') as f:
        f.write('\n'.join(L))
    print('wrote', os.path.relpath(OUT, REPO), '(%d maps)' % len(results))


# Remix enemies: a monster's sounds live in the ambient VAB of the maps it
# belongs to (its SFX ids name a program in "the" ambient bank, slot 2). Emit,
# per monster type, the ambient banks of the maps that host it natively
# (charaGroupIds), and the game's ambientAudioIdx -> bank load command table,
# so the runtime can load a home bank for a monster placed somewhere else.
SFX_OUT = os.path.join(REPO, 'pc_port', 'addons', 'remix', 'include', 'pc_remix_sfx_data.h')
SFX_CHARAS = ['GreyChild', 'Mumbler', 'Stalker', 'LarvalStalker', 'Creeper', 'Groaner', 'Wormhead', 'Romper',
              'PuppetNurse', 'PuppetDoctor', 'AirScreamer', 'NightFlutter', 'HangedScratcher']


def ambient_cmds():
    s = rd.strip_comments(open(os.path.join(REPO, 'src', 'bodyprog', 'game_boot', 'background_sound_init.c')).read())
    m = re.search(r'g_AmbientVabTaskLoadCmds\[(\d+)\]\s*=\s*\{(.*?)\}', s, re.S)
    vals = [int(v) for v in re.findall(r'\d+', m.group(2))]
    if len(vals) != int(m.group(1)):
        sys.exit('g_AmbientVabTaskLoadCmds: expected %s entries, got %d' % (m.group(1), len(vals)))
    return vals


def write_sfx_data():
    cmds = ambient_cmds()
    homes = {c: [] for c in SFX_CHARAS}
    for m in rd.MAP_NAMES:
        hdr = os.path.join(REPO, 'src', 'maps', m, m + '_header.c')
        if not os.path.exists(hdr):
            continue
        s = rd.strip_comments(open(hdr).read())
        g = re.search(r'\.charaGroupIds\s*=\s*\{([^}]*)\}', s)
        a = re.search(r'\.ambientAudioIdx\s*=\s*(\d+)', s)
        if not g or not a:
            continue
        cmd = cmds[int(a.group(1))]
        for c in re.findall(r'Chara_(\w+)', g.group(1)):
            if c in homes and cmd not in homes[c] and 170 <= cmd <= 204:
                homes[c].append(cmd)
    L = []
    w = L.append
    w('/* AUTO-GENERATED by pc_port/tools/gen_remix_data.py -- do not edit. */')
    w('/* SPDX-License-Identifier: GPL-3.0-or-later */')
    w('#ifndef PC_REMIX_SFX_DATA_H')
    w('#define PC_REMIX_SFX_DATA_H')
    w('')
    w('/* g_AmbientVabTaskLoadCmds (background_sound_init.c): map ambientAudioIdx -> VAB load command. */')
    w('static const unsigned char REMIX_AMBIENT_CMDS[%d] = { %s };' % (len(cmds), ', '.join(str(c) for c in cmds)))
    w('')
    w('/* Ambient banks (load commands, g_AudioData[cmd - 160]) of the maps each monster is native to. */')
    w('typedef struct { int chara; unsigned char count; unsigned char cmds[8]; } s_RemixCharaBanks;')
    w('static const s_RemixCharaBanks REMIX_CHARA_BANKS[] = {')
    for c in SFX_CHARAS:
        if not homes[c]:
            sys.exit('no native map found for Chara_%s' % c)
        w('    { Chara_%s, %d, { %s } },' % (c, len(homes[c][:8]), ', '.join(str(x) for x in homes[c][:8])))
    w('};')
    w('')
    w('#endif /* PC_REMIX_SFX_DATA_H */')
    w('')
    with open(SFX_OUT, 'w', newline='\n') as f:
        f.write('\n'.join(L))
    print('wrote', os.path.relpath(SFX_OUT, REPO))


if __name__ == '__main__':
    main()
    write_sfx_data()
