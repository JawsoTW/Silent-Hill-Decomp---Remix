/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_REMIX_LAYOUT_H
#define PC_REMIX_LAYOUT_H

/* Pure layout logic for the Remix mode (no game state), so it can be exercised
 * against the real generated tables outside the game: see
 * pc_port/tests/remix_layout_test.c and docs/Randomizer_Mode.md. */

#include <stddef.h>
#include <stdint.h>

#define REMIX_MAP_COUNT   48
#define REMIX_MAX_DOORS   96
#define REMIX_MAX_ROOMS   64
#define REMIX_MAX_SPOTS   64
#define REMIX_MAX_PICKUPS 32

typedef struct {
    uint16_t event; /* row in the map's MAP_EVENTS */
    uint8_t  poi;   /* doorway mapPoint, cross-checked at runtime */
    uint8_t  room;
    uint8_t  zone;
    uint8_t  twin;  /* vanilla partner, index into the same door table */
    uint8_t  ep;    /* vanilla eventParam (arrival mapPoint) */
    uint8_t  bgm;   /* vanilla mapIdx field: the BGM track on a room transition */
    uint8_t  rail;  /* vanilla field_8_24: arrival is on a camera rail */
} s_RemixDoor;

typedef struct {
    uint16_t event;
    uint8_t  poi;
    uint8_t  model; /* g_CommonWorldObjects[] index the map draws it with */
    int16_t  flag;  /* its disabledEventFlag: set once taken */
    const char* pose;    /* exported s_Pose (array) the map draws it with, NULL = none */
    uint8_t     poseIdx;
} s_RemixPickup;

/* Spot i is pickup i's own vanilla place (the generator emits them in pickup
 * order), so a pickup moved there is drawn with pickup i's pose. */
typedef struct {
    int32_t x, z; /* q19_12 */
    uint8_t room;
} s_RemixSpot;

#define REMIX_MAX_KEYS     16
#define REMIX_MAX_KEYSLOTS 64
#define REMIX_KEY_ROWS     4

/* A plain key-item pickup (Event_ItemTake of a non-common item) whose model the
 * map draws as <symbol>.object at <symbol>.position behind its pickup flag. */
typedef struct {
    const char* symbol;  /* exported map-DLL s_WorldObjectPose / s_WorldObjectPlacement */
    uint8_t     isPose;  /* s_WorldObjectPose: has a rotation to draw with */
    int16_t     flag;    /* pickup flag (set once taken) */
    uint8_t     slot;    /* original spot, index into keySlots */
    uint8_t     rowCount;
    struct { uint16_t event; uint8_t poi; int16_t requiredEventFlag; } rows[REMIX_KEY_ROWS];
    uint8_t     anywhere; /* optional bonus item: any slot on the map, zone or not */
    uint8_t     index;    /* element of <symbol> when it is an array */
    uint8_t     swap;     /* street key with no zone: trades places with a common pickup out on the open street */
    int16_t     unlockFlag; /* event flag its room needs (a bonus unlock), 0 = none */
    uint8_t     pinned;   /* runtime: stays on its spot, and the spot takes nothing else */
    uint64_t    reach;    /* keySlots it may also go to, beyond its zone's: proven reachable (and
                           * back) while it lies on its own spot (gen_remix_data.py, reach_set) */
    uint16_t    item;     /* InvItemId */
} s_RemixKey;

typedef struct {
    uint16_t    item;     /* InvItemId */
    uint8_t     kind;     /* 0 key item, 1 weapon, 2 unlockable */
    const char* name;     /* as remix_fixed_items names it */
} s_RemixItemName;

#define REMIX_NO_ZONE 254 /* a slot outside every zone: only `anywhere` items use it */

/* A spot a key may move to: a key's or a common pickup's original position or an
 * enemy spawn point, in a known zone (so reaching it needs exactly what reaching
 * the zone needs). Zones >= 128 are rooms behind a single gated door. */
typedef struct {
    int32_t x, z;    /* q19_12 */
    uint8_t zone;
    int16_t cellX;   /* floor(pos / 40): the IPD cell a chunk-local model resolves in */
    int16_t cellZ;
    uint8_t common;  /* index into pickups of the common pickup originally here, 255 = none */
    uint8_t swapOk;  /* a street key may trade places with the pickup here (STREET_SWAP_SPOTS) */
} s_RemixSlot;

/* An item that may trade places with one in another area (pc_remix.c,
 * "Between areas"). A key is drawn there from its own area's model files, read
 * into memory just for its model; a common pickup from the item model set every
 * area loads. */
typedef struct {
    uint8_t     map;         /* MapIdx of the area it lies in */
    uint8_t     isKey;
    uint8_t     local;       /* index into that area's keys (keys) or pickups */
    uint8_t     commonType;  /* CommonPickupItemId (commons) */
    uint16_t    item;        /* InvItemId (keys) */
    int16_t     flag;        /* its pickup flag */
    int16_t     reqFlag;     /* its pickup's required flag, 0 = none */
    int32_t     x, z;        /* its spot (q19_12) */
    int32_t     poseX, poseY, poseZ; /* keys: its pose (q19_12) */
    int16_t     rotX, rotY, rotZ;
    const char* takeMsg;     /* keys: the "Take it?" message */
    const char* model;       /* keys: its model's name */
    const char* ipdTag;      /* keys: its area's IPD file prefix */
    int8_t      cellX, cellZ;/* keys: the IPD cell its pose lies in */
    int16_t     plmFile;     /* keys: its area's global model set (e_FsFile), -1 = none */
    uint8_t     count;       /* keys: Event_ItemTake count (the Shotgun comes loaded) */
} s_RemixXItem;

typedef struct {
    uint16_t aFirst, aCount; /* items that may go to the other area (REMIX_X_ITEMS) */
    uint16_t bFirst, bCount; /* the items there they may trade with */
    uint16_t num, den;       /* each a item trades in num of den seeds (unless an earlier rule took it) */
    uint8_t  weapons;        /* a weapon's rule: only with remix_weapons */
} s_RemixXRule;

typedef struct {
    const s_RemixDoor*   doors;
    uint8_t              doorCount;
    uint8_t              zoneCount;
    const s_RemixPickup* pickups;
    uint8_t              pickupCount;
    const s_RemixSpot*   spots;
    uint8_t              spotCount;
    uint16_t             pointCount; /* MAP_POINTS entries */
    const s_RemixKey*    keys;
    uint8_t              keyCount;
    const s_RemixSlot*   keySlots;
    uint8_t              keySlotCount;
} s_RemixMap;

/* Pick a new partner for every free door. partner[i] is an index into m->doors;
 * the result is always symmetric (partner[partner[i]] == i), never joins a room
 * to itself, keeps every room's door count, and keeps every zone connected.
 * A zone that cannot be rebuilt keeps its vanilla pairing. Deterministic in
 * (seed, mapIdx). Returns how many zones were actually changed. */
int RemixLayout_Doors(const s_RemixMap* m, uint32_t seed, int mapIdx, uint8_t* partner);

/* Rooms across floors: zones of several maps (a building's floors) rewired as
 * one. Rooms of one merge set are one node of the rebuild (the stairs, and doors
 * always open both ways, join them), so the group stays connected however its
 * doors land. */
typedef struct {
    uint8_t map, zone;
} s_RemixGroupMember;
typedef struct {
    uint8_t map, room, set;
} s_RemixGroupMerge;
typedef struct {
    const s_RemixGroupMember* members;
    uint8_t                   memberCount;
    const s_RemixGroupMerge*  merges;
    uint8_t                   mergeCount;
} s_RemixDoorGroup;
typedef struct {
    uint8_t map, door; /* a door: its map, its index into that map's doors */
} s_RemixDoorRef;

/* The group's doors (refs[0..n), in member order) and a new partner for each
 * (partner[i], an index into refs): symmetric, never a door into its own room
 * or merged node, every room keeping its door count, the group connected.
 * Deterministic in (seed, groupIdx), so every map of the group gets the same
 * answer. maps is REMIX_MAPS. Returns n, with partner[] the vanilla pairing if
 * the group couldn't be rebuilt; 0 if it is too big. */
int RemixLayout_DoorGroup(const s_RemixDoorGroup* g, const s_RemixMap* maps, uint32_t seed, int groupIdx,
                          s_RemixDoorRef* refs, uint8_t* partner);

/* Pick a distinct spot for every pickup, spread across rooms first.
 * spot[i] is an index into m->spots. Spots with exclude[s] != 0 are never used
 * (exclude may be NULL); a pickup with no spot left keeps its own (spot[i] = 255).
 * Deterministic in (seed, mapIdx). */
void RemixLayout_Pickups(const s_RemixMap* m, uint32_t seed, int mapIdx, uint8_t* spot,
                         const uint8_t* exclude);

/* Key items. For every key, cell[k] is a slot in its own zone AND its own IPD cell
 * (the fallback: always visible, since a chunk-local model resolves there; usually
 * its original spot) and wide[k] a slot anywhere in its own zone (anywhere on
 * the map, for an `anywhere` item; for a `swap` key, a common pickup's spot out
 * on the open street, and that pickup goes to the key's spot). All cell slots
 * are distinct, and no wide slot is another key's wide or cell slot, so falling
 * back can never put two keys on one spot. reserved[s] marks every slot a key can
 * occupy: wide, cell, or its original spot (where it waits until classified and
 * returns as a last fallback). A key with no free slot keeps its original one. */
void RemixLayout_Keys(const s_RemixMap* m, uint32_t seed, int mapIdx,
                      uint8_t* wide, uint8_t* cell, uint8_t* reserved);

/* Slots nothing may take (an item from another area lies there), or NULL.
 * Read by RemixLayout_Keys and RemixLayout_KeyDisplaced (through reserved). */
extern const uint8_t* g_RemixLayoutBlocked;

/* Common pickups whose original spot a key may now occupy, when the items mode is
 * off: commonSlot[c] is a free slot in the same zone to move pickup c to, or 255
 * (stays, or nowhere free). */
void RemixLayout_KeyDisplaced(const s_RemixMap* m, uint32_t seed, int mapIdx,
                              const uint8_t* reserved, uint8_t* commonSlot);

#endif /* PC_REMIX_LAYOUT_H */
