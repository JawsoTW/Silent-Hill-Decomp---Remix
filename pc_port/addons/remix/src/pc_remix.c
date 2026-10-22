/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The randomizer (Remix in the code, its first name). Config keys remix_* in
 * randomizer.cfg; every entry point is a no-op when they are off, so vanilla
 * play is untouched.
 *
 * An interior in Silent Hill is a set of room islands scattered across the map
 * plane, joined only by SysState_LoadRoom rows. Each such door has a twin on the
 * far side whose doorway is exactly where the door drops you. pc_remix_data.h
 * (gen_remix_data.py) records every ungated door pair, which room each doorway
 * belongs to, and which "zone" (rooms joined by ungated doors) it is in.
 *
 * On map load we copy the live header (inheriting lang_text.c's translated copy) and, inside our copy:
 *   - rewire the ungated doors: door A joined to door C takes C's twin's
 *     (arrival mapPoint, BGM, camera-rail) -- the designer-authored landing for
 *     coming in through C. RemixLayout_Doors keeps every zone connected and every
 *     room's door count, and never crosses zones, so whatever keys or story flags
 *     gate (the fixed doors between zones) is exactly vanilla;
 *   - move each common pickup's trigger mapPoint to a new spot, and wrap the map's
 *     updateWorldObjects so the pickup's model is drawn there instead.
 * The layout is a pure function of (remix_seed, map), so a save reloads into the
 * same building. The DLL's own tables are never written.
 */

#include "game.h"
#include "bodyprog/bodyprog.h"
#include "bodyprog/collision/collision.h"
#include "bodyprog/gfx/world_object.h"
#include "bodyprog/map/map.h"
#include "bodyprog/savegame.h"
#include "bodyprog/screen/screen_fade.h"

#include "map_registry.h"
#include "pc_config.h"
#include "pc_remix.h"
#include "pc_remix_cfg.h"
#include "pc_remix_data.h"
#include "pc_wide_lm.h"
#include "sh_log.h"
#include "main/fileinfo.h"
#include "main/fsqueue.h"
#include "bodyprog/items.h"
#include "bodyprog/item_screens.h"
#include "bodyprog/events/map_msg.h"
#include "pc_playas.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Event_Update walks at most this many rows. */
#define REMIX_MAX_EVENTS 256
/* pointOfInterestIdx is a u8. */
#define REMIX_MAX_POINTS 256

/* A moved pickup is drawn once the player is this close; interiors fog out well
 * before it, and collision for its room is resident by then. */
#define REMIX_PICKUP_DRAW_DIST Q12(24.0f)

/* Runtime working storage the map's initWorldObjects fills (EXE-side, see
 * data_stubs.c); indices 0..5 are the six common pickups. */
extern s_WorldObjectModel g_CommonWorldObjects[];

/* Read from g_PcConfig on every map load, not cached at boot, so the PC
 * Options Remix rows take effect on the next area without a restart. */
static u32      s_seed;

/* Engine functions with no header declaration. */
extern u32                 Fs_QueueEntryLoadStatusGet(s32 queueIdx);
extern s_IpdCollisionData* func_800426E4(s32 posX, s32 posZ);
extern s_MapTerrain        g_Map;
extern void*               MapOverlay_GetSymbol(const char* name);
extern s32                 Map_IpdIdxGet(s32 cellX, s32 cellZ);
extern bool                IpdHeader_PC_IsReformatted(s_IpdHeader* ipdHdr);

/* A moved key is checked for reachability once the player is this close: its
 * spot's collision is resident by then, so "no floor" means no floor. */
#define REMIX_KEY_PROBE_DIST Q12(10.0f)
/* Frames to wait for the map's global model set before classifying keys without it. */
#define REMIX_KEY_CLASSIFY_FRAMES 90

/* A key whose model lives in its room's geometry is carried by a copy of that
 * model; it waits at its own spot until the copy exists, for at most this long. */
#define REMIX_COPY_TIMEOUT_FRAMES 1800
#define REMIX_COPY_MAX_TEX        16
/* The vertex/normal/shade arrays are read past their end by the strided GTE
 * copies (lm_validate.c, rule V9); the copy carries the same zeroed tail. */
#define REMIX_COPY_SLACK          64

enum { KEY_PENDING = 0, KEY_WIDE, KEY_CELL, KEY_HOME, KEY_COPYING, KEY_WAITMOVE };

/* A chunk-local model, copied out of its chunk so it draws anywhere: the
 * reformatted mesh headers point into the chunk's raw IPD buffer, which is
 * reused as soon as the chunk slot is, so every mesh array is copied. The
 * primitives carry their texture page and CLUT, and the pool slots behind
 * them are pinned (refCount) for as long as the copy lives. Drawn with lmIdx 2
 * (the global set's index), which the draw path never re-checks against a
 * chunk. */
typedef struct {
    s_WorldObjectModel obj;
    s_ModelHeader      hdr;
    s_MeshHeader*      meshes;
    u8*                block;
    s_Texture*         tex[REMIX_COPY_MAX_TEX];
    u_Filename         texName[REMIX_COPY_MAX_TEX];
    int                texCount;
    int                built;
} s_KeyCopy;

/* Layout of maps/shared.h's s_WorldObjectPose (a map-side header the exe does not
 * include): s_WorldObjectPlacement plus a rotation. */
typedef struct {
    s_WorldObjectModel object;
    VECTOR3            position;
    SVECTOR3           rotation;
} s_RemixPose;
_Static_assert(offsetof(s_RemixPose, position) == offsetof(s_WorldObjectPlacement, position),
               "s_WorldObjectPose and s_WorldObjectPlacement must share their prefix");

typedef struct {
    s_WorldObjectModel* model;  /* <symbol>.object, at offset 0 of the map's pose/placement */
    const VECTOR3*      origPos;/* <symbol>.position: what the map passes when it draws it */
    const SVECTOR3*     rot;    /* s_WorldObjectPose only */
    uint8_t             enabled;
    uint8_t             state;
    uint8_t             wide, cell, cur; /* key slots */
    uint8_t             poi[REMIX_KEY_ROWS]; /* its pickup rows' own map points (see Pc_Remix_OnMapLoad) */
    q19_12              dynX, dynZ;          /* where it is now */
    uint8_t             pendState, pendSlot; /* KEY_WAITMOVE: where it goes once unseen */
    const VECTOR3*      dynPos;              /* the pose it is drawn with there */
    const SVECTOR3*     dynRot;
    VECTOR3             dynPosBuf;
    uint8_t             drawsAnywhere;       /* its model is in the global or item set */
    uint8_t             bottomKnown;
    q19_12              bottom;              /* see model_bottom */
    int                 copyFrames;
    s_KeyCopy           copy;
} s_KeyRt;

static int               s_active;
static const s_RemixMap* s_map;
static s_MapOverlayHdr   s_hdr;
static s_EventData       s_events[REMIX_MAX_EVENTS + 1];
static const s_EventData* s_srcEvents; /* the map's own table, for the vanilla trigger types */
static const s_MapPoint2d* s_srcPoints; /* and its points, for the vanilla trigger shapes */
static s_MapPoint2d      s_points[REMIX_MAX_POINTS];
static void            (*s_origUpdateWorldObjects)(void);

static uint8_t           s_commonMoved[REMIX_MAX_PICKUPS];
static q19_12            s_commonX[REMIX_MAX_PICKUPS];
static q19_12            s_commonZ[REMIX_MAX_PICKUPS];
static const VECTOR3*    s_commonPos[REMIX_MAX_PICKUPS];
static const SVECTOR3*   s_commonRot[REMIX_MAX_PICKUPS];
static const s_Pose*     s_pickupPose[REMIX_MAX_PICKUPS]; /* each pickup's own vanilla pose */
static uint8_t           s_commonOnSlot[REMIX_MAX_PICKUPS]; /* key slot a pickup was moved to, 255 = none */

static int               s_keysOn;
static s_KeyRt           s_keys[REMIX_MAX_KEYS];
static int               s_keyFrames;
static int               s_filterOn;

int Pc_Remix_MapActive(void) { return s_active; }

static int map_selected(s32 mapIdx)
{
    const char* list = g_RemixCfg.remixMaps;
    const char* name = MapRegistry_GetName((e_MapIdx)mapIdx);
    size_t      n;

    if (list[0] == '\0')
        return 1;
    if (name == NULL)
        return 0;

    n = strlen(name);
    while (*list)
    {
        while (*list == ',' || *list == ' ')
            list++;
        if (strncmp(list, name, n) == 0 && (list[n] == '\0' || list[n] == ',' || list[n] == ' '))
            return 1;
        while (*list && *list != ',')
            list++;
    }
    return 0;
}

/* The generated tables index rows of the map's MAP_EVENTS as written in the
 * source. Refuse the map outright if the live table disagrees anywhere (a mod
 * DLL, a source edit without regenerating) rather than rewire the wrong rows. */
static int tables_match(const s_RemixMap* m, const s_EventData* ev, int nEvents, const s_MapPoint2d* pts)
{
    int i, j;

    if (pts == NULL || m->pointCount > REMIX_MAX_POINTS)
        return 0;

    for (i = 0; i < m->doorCount; i++)
    {
        const s_RemixDoor* d = &m->doors[i];
        const s_EventData* e;
        if (d->event >= nEvents || d->twin >= m->doorCount)
            return 0;
        e = &ev[d->event];
        if (e->sysState != SysState_LoadRoom || e->pointOfInterestIdx != d->poi ||
            e->eventParam != d->ep || d->ep >= m->pointCount)
            return 0;
    }

    for (i = 0; i < m->pickupCount; i++)
    {
        const s_RemixPickup* p = &m->pickups[i];
        const s_EventData*   e;
        if (p->event >= nEvents || p->poi >= m->pointCount || p->model >= 6)
            return 0;
        e = &ev[p->event];
        if (e->sysState != SysState_EventCallback || e->pointOfInterestIdx != p->poi ||
            e->disabledEventFlag != p->flag)
            return 0;
    }

    for (i = 0; i < m->keyCount; i++)
    {
        const s_RemixKey* k = &m->keys[i];
        if (k->slot >= m->keySlotCount || k->rowCount > REMIX_KEY_ROWS)
            return 0;
        for (j = 0; j < k->rowCount; j++)
        {
            const s_EventData* e;
            if (k->rows[j].event >= nEvents || k->rows[j].poi >= m->pointCount)
                return 0;
            e = &ev[k->rows[j].event];
            if (e->sysState != SysState_EventCallback || e->pointOfInterestIdx != k->rows[j].poi ||
                e->requiredEventFlag != k->rows[j].requiredEventFlag)
                return 0;
        }
    }
    for (i = 0; i < m->keySlotCount; i++)
        if (m->keySlots[i].common != 255 && m->keySlots[i].common >= m->pickupCount)
            return 0;
    return 1;
}

static s64 dist2(q19_12 ax, q19_12 az, q19_12 bx, q19_12 bz)
{
    s64 dx = (s64)ax - bx, dz = (s64)az - bz;
    return dx * dx + dz * dz;
}

/* A common pickup on a spot is drawn the way the map drew the pickup that was
 * there: on its shelf, at its angle. A key's pose is no use for that: key
 * models are mostly room geometry whose mesh sits metres away from their pose
 * origin (the hospital's Plate of Cat draws 3.5 m from (-20, -19.2)), so on a
 * key's spot it goes on the floor at the spot itself. */
static void slot_pose(int slot, const VECTOR3** pos, const SVECTOR3** rot)
{
    int c;

    *pos = NULL;
    *rot = NULL;
    c = s_map->keySlots[slot].common;
    if (c != 255 && s_pickupPose[c] != NULL)
    {
        *pos = &s_pickupPose[c]->position;
        *rot = &s_pickupPose[c]->rotation;
    }
}

/* The pickup row of the item whose vanilla place a spot is: a key's first row,
 * or a common pickup's. 0 = an enemy spawn point, nobody's place. */
static int slot_owner(int slot, int* event, int* poi)
{
    int k, c;

    for (k = 0; k < s_map->keyCount && k < REMIX_MAX_KEYS; k++)
        if (s_map->keys[k].slot == slot && s_map->keys[k].rowCount > 0)
        {
            *event = s_map->keys[k].rows[0].event;
            *poi   = s_map->keys[k].rows[0].poi;
            return 1;
        }
    c = s_map->keySlots[slot].common;
    if (c != 255)
    {
        *event = s_map->pickups[c].event;
        *poi   = s_map->pickups[c].poi;
        return 1;
    }
    return 0;
}

/* An item moved onto another item's place picks up there exactly as that item
 * did: its trigger type and shape (the facing line along the desk edge, or the
 * reach) come from the place's own pickup row, so it is reachable from wherever
 * the original was. */
static void take_place_trigger(int event, s_MapPoint2d* pt, int slot)
{
    int oe, op;

    if (!slot_owner(slot, &oe, &op))
        return;
    s_events[event].triggerType = s_srcEvents[oe].triggerType;
    pt->triggerParam0           = s_srcPoints[op].triggerParam0;
    pt->triggerParam1           = s_srcPoints[op].triggerParam1;
}

/* A pickup row's TouchObbFacing trigger is a short line laid along the edge of
 * the item's own desk or shelf; you reach across it from the side the map
 * expects. Moved to another item's place, that line keeps its old angle, and
 * from the new spot you can end up facing along it and never reach it. A moved
 * item uses the plain facing trigger instead: be within reach (0.8 m) and face
 * it, from any side. Back home it gets its vanilla trigger again. */
static void pickup_trigger(int event, int moved)
{
    if (s_srcEvents[event].triggerType != TriggerType_TouchObbFacing)
        return;
    s_events[event].triggerType = moved ? TriggerType_TouchFacing : TriggerType_TouchObbFacing;
}

/* ------------------------------------------------------------------ keys */

/* A key's spot is its pickup point, where the item is seen. Its model is drawn
 * with its own pose, shifted by however far the spot is from its own: the mesh
 * keeps its offset from the pose origin, so it lands on the spot. */
static void key_place_xz(int k, int slot, q19_12 x, q19_12 z)
{
    const s_RemixKey*  key  = &s_map->keys[k];
    const s_RemixSlot* home = &s_map->keySlots[key->slot];
    s_KeyRt*           kr   = &s_keys[k];
    int                j;

    kr->dynRot = kr->rot;
    kr->dynPos = kr->origPos;
    if (slot != key->slot && kr->origPos != NULL)
    {
        kr->dynPosBuf     = *kr->origPos;
        kr->dynPosBuf.vx += x - home->x;
        kr->dynPosBuf.vz += z - home->z;
        kr->dynPos        = &kr->dynPosBuf;
    }
    kr->cur  = (uint8_t)slot;
    kr->dynX = x;
    kr->dynZ = z;
    for (j = 0; j < key->rowCount; j++)
    {
        s_MapPoint2d* pt = &s_points[kr->poi[j]];

        *pt = s_srcPoints[key->rows[j].poi];
        s_events[key->rows[j].event].triggerType = s_srcEvents[key->rows[j].event].triggerType;
        /* Home: its own vanilla point (two keys can share one home slot,
         * like the pliers and screwdriver, yet lie apart). */
        if (slot == key->slot)
            continue;
        pickup_trigger(key->rows[j].event, 1);
        take_place_trigger(key->rows[j].event, pt, slot);
        pt->positionX = x;
        pt->positionZ = z;
    }
}

static void key_place(int k, int slot)
{
    key_place_xz(k, slot, s_map->keySlots[slot].x, s_map->keySlots[slot].z);
}

/* One step down the fallback ladder: wide -> its own cell -> its vanilla spot.
 * Each rung is strictly safer: the cell slot resolves a chunk-local model, and
 * the vanilla spot is exactly the original game. */
static void key_fall_back(int k, const char* why)
{
    s_KeyRt* kr = &s_keys[k];

    if (kr->state == KEY_WIDE && kr->cell != kr->wide)
    {
        kr->state = KEY_CELL;
        key_place(k, kr->cell);
    }
    else if (kr->state != KEY_HOME)
    {
        kr->state = KEY_HOME;
        key_place(k, s_map->keys[k].slot);
    }
    SH_LOG("[RANDOMIZER] key %s: %s -> %s", s_map->keys[k].symbol, why,
           kr->state == KEY_CELL ? "moved to a spot in its own cell" : "back to its original spot");
}

/* Fading out, black, or fading back in: an area is entered this way, so a key
 * whose room the player starts in -- the cafe -- moves while the screen is
 * still coming up from black, not in front of them. */
static int screen_dark(void)
{
    s32 st = g_Screen_FadeStatus & 0x7;
    return st >= ScreenFadeState_FadeOutStart && st <= ScreenFadeState_FadeInSteps;
}

/* A key may change spots only where the player can't watch it happen. */
static int move_unseen(int k, int slot)
{
    const VECTOR3*     p    = &g_SysWork.playerWork.player.position;
    const s_RemixSlot* home = &s_map->keySlots[s_map->keys[k].slot];
    const s_RemixSlot* dest = &s_map->keySlots[slot];
    s64                d2   = (s64)REMIX_PICKUP_DRAW_DIST * REMIX_PICKUP_DRAW_DIST;

    return screen_dark() || (dist2(home->x, home->z, p->vx, p->vz) > d2 && dist2(dest->x, dest->z, p->vx, p->vz) > d2);
}

/* ------------------------------------------------------------ model copy */

static void copy_release(s_KeyCopy* c, int mapGone)
{
    int i;

    for (i = 0; i < c->texCount; i++)
    {
        s_Texture* tex = c->tex[i];
        /* A slot the engine reset meanwhile is not ours to touch. */
        if (COMPARE_FILENAMES(&tex->name, &c->texName[i]) || tex->refCount <= 0)
            continue;
        tex->refCount--;
        /* Leaving the map: do what Ipd_TexturesRefClear would have done to it
         * had the pin not kept it alive, so the next map cannot match a stale
         * TIM by name. */
        if (mapGone && tex->refCount == 0)
            Texture_RefClear(tex);
    }
    free(c->meshes);
    free(c->block);
    memset(c, 0, sizeof(*c));
}

static size_t copy_round(size_t n)
{
    return (n + 7) & ~(size_t)7;
}

static int copy_build(s_KeyCopy* c, const s_WorldObjectModel* src, const s_ModelHeader* mh, const s_LmHeader* lm)
{
    size_t total = 0, off = 0;
    int    m, i, j;

    memset(c, 0, sizeof(*c));
    if (mh->meshCount == 0 || mh->meshHdrs == NULL || Pc_WideLm_IsWide(mh))
        return 0;

    for (m = 0; m < mh->meshCount; m++)
    {
        const s_MeshHeader* sm = &mh->meshHdrs[m];
        total += copy_round(sm->primitiveCount * sizeof(s_Primitive)) +
                 copy_round(sm->vertexCount * sizeof(DVECTOR) + REMIX_COPY_SLACK) +
                 copy_round(sm->vertexCount * sizeof(s16) + REMIX_COPY_SLACK) +
                 copy_round(sm->normalCount * sizeof(s_Normal) + REMIX_COPY_SLACK) +
                 copy_round(sm->unkCount_3 + REMIX_COPY_SLACK);

        for (i = 0; i < sm->primitiveCount; i++)
        {
            s32        matIdx = sm->primitives[i].field_6.bits.materialIdx;
            s_Texture* tex;

            if (matIdx < 0)
                continue;
            if (matIdx >= lm->materialCount)
                return 0;
            if (lm->materials[matIdx].field_C != 0)
                continue;
            tex = lm->materials[matIdx].texture;
            if (tex == NULL)
                return 0;
            for (j = 0; j < c->texCount && c->tex[j] != tex; j++)
                ;
            if (j == c->texCount)
            {
                if (c->texCount == REMIX_COPY_MAX_TEX)
                    return 0;
                c->tex[c->texCount++] = tex;
            }
        }
    }

    c->meshes = (s_MeshHeader*)calloc(mh->meshCount, sizeof(s_MeshHeader));
    c->block  = (u8*)calloc(1, total);
    if (c->meshes == NULL || c->block == NULL)
    {
        free(c->meshes);
        free(c->block);
        memset(c, 0, sizeof(*c));
        return 0;
    }

#define COPY_ARRAY(field, bytes, slack)                         \
    do {                                                        \
        size_t _n = (bytes);                                    \
        memcpy(c->block + off, sm->field, _n);                  \
        dm->field = (void*)(c->block + off);                    \
        off += copy_round(_n + (slack));                        \
    } while (0)

    for (m = 0; m < mh->meshCount; m++)
    {
        const s_MeshHeader* sm = &mh->meshHdrs[m];
        s_MeshHeader*       dm = &c->meshes[m];

        *dm = *sm;
        COPY_ARRAY(primitives, sm->primitiveCount * sizeof(s_Primitive), 0);
        COPY_ARRAY(verticesXy, sm->vertexCount * sizeof(DVECTOR), REMIX_COPY_SLACK);
        COPY_ARRAY(verticesZ, sm->vertexCount * sizeof(s16), REMIX_COPY_SLACK);
        COPY_ARRAY(normals, sm->normalCount * sizeof(s_Normal), REMIX_COPY_SLACK);
        COPY_ARRAY(unkPtr_14, sm->unkCount_3, REMIX_COPY_SLACK);
    }
#undef COPY_ARRAY

    for (i = 0; i < c->texCount; i++)
    {
        c->tex[i]->refCount++;
        c->texName[i] = c->tex[i]->name;
    }

    c->hdr                     = *mh;
    c->hdr.meshHdrs            = c->meshes;
    c->obj                     = *src;
    c->obj.modelInfo.field_0   = 0;
    c->obj.modelInfo.modelHdr  = &c->hdr;
    c->obj.metadata.lmIdx      = 2;
    c->built                   = 1;
    return 1;
}

static int chunk_geom_ready(s_Chunk* ch)
{
    return ch->queueIdx != NO_VALUE && Fs_QueueEntryLoadStatusGet(ch->queueIdx) >= ChunkLoadState_Loaded &&
           ch->ipdHdr != NULL && IpdHeader_PC_IsReformatted(ch->ipdHdr) &&
           ch->ipdHdr->isLoaded && ch->ipdHdr->lmHdr != NULL;
}

/* A chunk whose geometry is reformatted and whose every material has a
 * resident texture: exactly what the engine itself waits for before drawing. */
static int chunk_ready(s_Chunk* ch)
{
    return chunk_geom_ready(ch) && IpdHeader_LoadStateGet(ch) >= StaticModelLoadState_Loaded;
}

/* Copies pin texture pool slots, which needs the expanded pool's virtual
 * slots (resident_textures). Interiors texture every loaded chunk on their
 * own; on exteriors the copy textures its chunk itself for a moment. */
static int copy_supported(void)
{
    return g_PcConfig.residentTextures;
}

/* Exteriors texture only the chunks around the player (that IS their draw
 * distance), so a far chunk's materials are claimed here for just long enough
 * to stamp its primitives and copy the model, then let go again exactly as the
 * engine lets go of a far chunk. The copy's own pins keep its slots; the chunk
 * itself goes back to untextured, so it is not drawn through the fog. */
static int copy_build_exterior(s_KeyCopy* c, const s_WorldObjectModel* src, const s_ModelHeader* mh,
                               s_IpdHeader* ipd)
{
    s_LmHeader* lm = ipd->lmHdr;
    uint8_t     claimed[256];
    int         i, ok;

    memset(claimed, 0, sizeof(claimed));
    for (i = 0; i < lm->materialCount && i < 256; i++)
        claimed[i] = lm->materials[i].texture == NULL;

    Ipd_MaterialsLoad(ipd, &g_Map.chunkTextures.fullPage, &g_Map.chunkTextures.halfPage, g_Map.textureFileIdx);
    Lm_MaterialFlagsApply(lm);
    ok = copy_build(c, src, mh, lm);

    for (i = 0; i < lm->materialCount && i < 256; i++)
    {
        s_Texture* tex = lm->materials[i].texture;
        if (!claimed[i] || tex == NULL)
            continue;
        if (tex->refCount > 0)
            tex->refCount--;
        lm->materials[i].texture = NULL;
    }
    return ok;
}

/* The TIMs behind a copy's pins may still be loading (an exterior copy claims
 * them itself); it moves only once they are in. */
static int copy_textures_loaded(const s_KeyCopy* c)
{
    int i;
    for (i = 0; i < c->texCount; i++)
        if (!Fs_QueueIsEntryLoaded(c->tex[i]->queueIdx))
            return 0;
    return 1;
}

/* 1 = copied, 0 = not yet, -1 = cannot be copied. */
extern bool IpdHeader_FixOffsets_PC(s_IpdHeader* ipdHdr);
extern void LmHeader_FixOffsets(s_LmHeader* lmHdr);
static u8* xmap_read(const char* name, s32 type, s32 fileIdx);
static int xmap_copy_from(s_KeyCopy* c, const char* name, s_LmHeader* lm, s_IpdHeader* ipd);

/* An interior streams only the cells around the player (two chunk slots, both
 * in use), so a key's home cell may not come in until the player walks there --
 * and until then the key would wait at home, wherever its seed puts it. Its
 * cell's file is read straight off the disc instead, as for an item from
 * another area. */
static int key_copy_from_disc(int k)
{
    const s_RemixSlot* home = &s_map->keySlots[s_map->keys[k].slot];
    s_KeyRt*           kr   = &s_keys[k];
    char               name[9];
    int                ring, dx, dz;

    memcpy(name, kr->model->metadata.name.str, 8);
    name[8] = '\0';
    for (ring = 0; ring < 2; ring++)
        for (dz = -ring; dz <= ring; dz++)
            for (dx = -ring; dx <= ring; dx++)
            {
                s32          fileIdx;
                u8*          buf;
                s_IpdHeader* ipd;
                int          ok;

                if ((dx < 0 ? -dx : dx) != ring && (dz < 0 ? -dz : dz) != ring)
                    continue;
                fileIdx = Map_IpdIdxGet(home->cellX + dx, home->cellZ + dz);
                if (fileIdx == NO_VALUE || (buf = xmap_read("", FileType_Ipd, fileIdx)) == NULL)
                    continue;
                ipd = (s_IpdHeader*)buf;
                ok  = IpdHeader_FixOffsets_PC(ipd);
                if (ok)
                {
                    ipd->isLoaded = true;
                    LmHeader_FixOffsets(ipd->lmHdr);
                    ok = xmap_copy_from(&kr->copy, name, ipd->lmHdr, ipd);
                    free(ipd->modelInfo);
                    free(ipd->modelBuffers);
                }
                if (ok)
                {
                    SH_LOG("[RANDOMIZER] key %s ('%s'): model read from its cell's file (%d,%d)", s_map->keys[k].symbol,
                           name, home->cellX + dx, home->cellZ + dz);
                    return 1;
                }
            }
    return 0;
}

static int key_copy_try(int k)
{
    const s_RemixSlot* home = &s_map->keySlots[s_map->keys[k].slot];
    s_KeyRt*           kr   = &s_keys[k];
    s_Chunk*           homeChunk = NULL;
    int                pass, i;

    for (i = 0; i < g_Map.activeChunkCount; i++)
    {
        s_Chunk* ch = &g_Map.activeChunks[i];
        if (ch->queueIdx != NO_VALUE && ch->cellX == home->cellX && ch->cellZ == home->cellZ)
            homeChunk = ch;
    }
    if (homeChunk == NULL)
    {
        /* Exteriors preload every chunk (preload_chunks); without it, wait for
         * the streamer to bring it in. */
        if (g_Map.isExterior)
            return 0;
        return key_copy_from_disc(k) ? 1 : -1;
    }
    if (!(g_Map.isExterior ? chunk_geom_ready(homeChunk) : chunk_ready(homeChunk)))
        return 0;

    /* Its own cell first; a neighbour only if that is where the model is
     * (seen from across a cell border). */
    for (pass = 0; pass < 2; pass++)
    {
        for (i = 0; i < g_Map.activeChunkCount; i++)
        {
            s_Chunk*           ch = &g_Map.activeChunks[i];
            s_WorldObjectModel tmp;
            int                dx = ch->cellX - home->cellX, dz = ch->cellZ - home->cellZ;
            int                ring = (dx < 0 ? -dx : dx) > (dz < 0 ? -dz : dz) ? (dx < 0 ? -dx : dx)
                                                                               : (dz < 0 ? -dz : dz);

            if (ring != pass || !(g_Map.isExterior ? chunk_geom_ready(ch) : chunk_ready(ch)))
                continue;
            tmp                  = *kr->model;
            tmp.metadata.lmIdx   = 0;
            if (!Lm_ModelFind(&tmp, ch->ipdHdr->lmHdr, &tmp.metadata))
                continue;
            if (g_Map.isExterior && !chunk_ready(ch))
                return copy_build_exterior(&kr->copy, kr->model, tmp.modelInfo.modelHdr, ch->ipdHdr) ? 1 : -1;
            return copy_build(&kr->copy, kr->model, tmp.modelInfo.modelHdr, ch->ipdHdr->lmHdr) ? 1 : -1;
        }
    }
    return -1;
}

static void key_place(int k, int slot);
static int  copy_textures_loaded(const s_KeyCopy* c);

/* Where key k is about to go, if it would move this frame: -1 if not. */
static int key_ready_target(int k)
{
    s_KeyRt* kr = &s_keys[k];

    if (kr->state == KEY_WAITMOVE)
        return move_unseen(k, kr->pendSlot) ? kr->pendSlot : -1;
    if (kr->state == KEY_COPYING && kr->copy.built && copy_textures_loaded(&kr->copy) && move_unseen(k, kr->wide))
        return kr->wide;
    return -1;
}

/* Street keys may take each other's spots. A key only moves onto another key's
 * spot once that key leaves it (in the same frame is fine, so two keys trading
 * spots never wait on each other); a key that has settled there for good (its
 * fallback home) doesn't hold anyone back. */
static int key_spot_clear(int k, int slot, int depth)
{
    int j;

    if (depth > REMIX_MAX_KEYS)
        return 1;
    for (j = 0; j < s_map->keyCount && j < REMIX_MAX_KEYS; j++)
    {
        s_KeyRt* kj = &s_keys[j];
        int      t;
        if (j == k || !kj->enabled || kj->cur != slot || Savegame_EventFlagGet(s_map->keys[j].flag))
            continue;
        if (kj->state != KEY_PENDING && kj->state != KEY_COPYING && kj->state != KEY_WAITMOVE)
            continue;
        t = key_ready_target(j);
        if (t < 0 || t == slot || !key_spot_clear(j, t, depth + 1))
            return 0;
    }
    return 1;
}

/* Take a new spot now, or -- in plain view -- wait at home until unseen. */
static void key_go(int k, int state, int slot)
{
    s_KeyRt* kr = &s_keys[k];

    if (slot == s_map->keys[k].slot || (move_unseen(k, slot) && key_spot_clear(k, slot, 0)))
    {
        kr->state = (uint8_t)state;
        key_place(k, slot);
        return;
    }
    kr->pendState = (uint8_t)state;
    kr->pendSlot  = (uint8_t)slot;
    kr->state     = KEY_WAITMOVE;
    key_place(k, s_map->keys[k].slot);
}

/* COPYING: the key waits at its own spot (the map draws it there) until its
 * model is copied, then moves -- but only while the player is clear of both
 * spots, so it never vanishes or appears in view. */
static void keys_copy_update(void)
{
    int k;

    for (k = 0; k < s_map->keyCount; k++)
    {
        s_KeyRt* kr = &s_keys[k];
        int      r;

        if (!kr->enabled)
            continue;
        if (kr->copy.built && Savegame_EventFlagGet(s_map->keys[k].flag))
        {
            copy_release(&kr->copy, 0);
            continue;
        }
        if (kr->state == KEY_WAITMOVE)
        {
            if (Savegame_EventFlagGet(s_map->keys[k].flag))
                kr->state = KEY_HOME;
            else if (move_unseen(k, kr->pendSlot) && key_spot_clear(k, kr->pendSlot, 0))
            {
                kr->state = kr->pendState;
                key_place(k, kr->pendSlot);
            }
            continue;
        }
        if (kr->state != KEY_COPYING)
            continue;
        if (Savegame_EventFlagGet(s_map->keys[k].flag))
        {
            kr->state = KEY_HOME;
            continue;
        }

        if (!kr->copy.built)
        {
            r = key_copy_try(k);
            if (r < 0 || (r == 0 && ++kr->copyFrames > REMIX_COPY_TIMEOUT_FRAMES))
            {
                key_go(k, KEY_CELL, kr->cell);
                SH_LOG("[RANDOMIZER] key %s: model could not be copied out of its room -> %s",
                       s_map->keys[k].symbol,
                       kr->cur == s_map->keys[k].slot ? "stays at its original spot" : "moved within its own cell");
                continue;
            }
            if (r == 0)
                continue;
            SH_LOG("[RANDOMIZER] key %s ('%.8s'): model copied out of its room (%d mesh(es), %d texture(s) pinned)",
                   s_map->keys[k].symbol, kr->model->metadata.name.str, kr->copy.hdr.meshCount,
                   kr->copy.texCount);
        }

        if (!copy_textures_loaded(&kr->copy))
        {
            if (++kr->copyFrames > REMIX_COPY_TIMEOUT_FRAMES)
            {
                copy_release(&kr->copy, 0);
                key_go(k, KEY_CELL, kr->cell);
                SH_LOG("[RANDOMIZER] key %s: its textures never loaded -> %s", s_map->keys[k].symbol,
                       kr->cur == s_map->keys[k].slot ? "stays at its original spot" : "moved within its own cell");
            }
            continue;
        }
        if (!move_unseen(k, kr->wide) || !key_spot_clear(k, kr->wide, 0))
            continue;
        kr->state = KEY_WIDE;
        key_place(k, kr->wide);
        SH_LOG("[RANDOMIZER] key %s: moved %s", s_map->keys[k].symbol,
               s_map->keys[k].swap       ? "to another item's spot before the door it opens"
               : s_map->keys[k].anywhere ? "to another item spot on the map"
                                         : "within its zone");
    }
}

static void keys_release_all(int mapGone)
{
    int k;
    for (k = 0; k < REMIX_MAX_KEYS; k++)
        if (s_keys[k].copy.built)
            copy_release(&s_keys[k].copy, mapGone);
}

/* The item model set (BG_ITEM.PLM) is read straight into g_WorldGfxWork.itemLmHdr
 * and only becomes a valid 64-bit header once LmHeader_FixOffsets_PC has run on
 * it, a frame or more after the read lands; until then its fields are raw PSX
 * bytes and its model-list pointer is garbage. isLoaded is what marks it fixed,
 * and it is what the engine itself checks. Nothing here may touch the item set --
 * directly, or through WorldGfx_ObjectAdd's fallback to it when it draws a
 * moved pickup or key -- before then. */
static int item_lm_ready(void)
{
    return g_WorldGfxWork.itemLmHdr.isLoaded != 0;
}

/* Is this model in a model set that draws anywhere? The map's global LM is
 * looked up first by func_8004287C regardless of position, and the item LM is
 * WorldGfx_ObjectAdd's own fallback; a model in neither lives in a chunk and
 * only resolves while the player stands in that chunk's cell. Works on a copy:
 * Lm_ModelFind writes into the model it is given. */
static int model_draws_anywhere(const s_WorldObjectModel* model, int globalReady)
{
    s_WorldObjectModel tmp = *model;

    if (tmp.metadata.name.str[0] == '\0')
        return 0;
    if (globalReady && Lm_ModelFind(&tmp, g_Map.globalLm.lmHdr, &tmp.metadata))
        return 1;
    if (item_lm_ready() && Lm_ModelFind(&tmp, &g_WorldGfxWork.itemLmHdr, &tmp.metadata))
        return 1;
    return 0;
}

static int plate_of_item(int item);
static int plate_usable(int p);

static void keys_classify(void)
{
    int globalReady, k, pending = 0;

    for (k = 0; k < s_map->keyCount; k++)
        pending |= s_keys[k].enabled && s_keys[k].state == KEY_PENDING;
    if (!pending)
        return;

    globalReady = g_Map.globalLm.lmHdr != NULL &&
                  Fs_QueueEntryLoadStatusGet(g_Map.globalLm.queueIdx) >= ChunkLoadState_Loaded &&
                  g_Map.globalLm.lmHdr->isLoaded;
    /* Wait for both sets; on timeout, whatever is not ready simply does not
     * count, which can only push a key toward its (always safe) cell spot. */
    if ((!globalReady || !item_lm_ready()) && ++s_keyFrames < REMIX_KEY_CLASSIFY_FRAMES)
        return;

    for (k = 0; k < s_map->keyCount; k++)
    {
        s_KeyRt* kr = &s_keys[k];
        if (!kr->enabled || kr->state != KEY_PENDING)
            continue;
        if (model_draws_anywhere(kr->model, globalReady) || plate_usable(plate_of_item(s_map->keys[k].item)))
        {
            kr->drawsAnywhere = model_draws_anywhere(kr->model, globalReady);
            kr->state = KEY_WIDE;
            key_place(k, kr->wide);
        }
        else if (copy_supported() && kr->wide != s_map->keys[k].slot && kr->wide != kr->cell)
        {
            /* Stays put (cur is its own slot) until keys_copy_update moves it. */
            kr->state = KEY_COPYING;
        }
        else
        {
            /* Not moving: home. Only a key bound elsewhere falls back to its cell. */
            kr->state = KEY_CELL;
            key_place(k, kr->wide == s_map->keys[k].slot ? kr->wide : kr->cell);
        }
        SH_LOG("[RANDOMIZER] key %s ('%.8s'): model %s -> %s", s_map->keys[k].symbol,
               kr->model->metadata.name.str,
               kr->state == KEY_WIDE ? (kr->drawsAnywhere ? "draws anywhere" : "drawn with its inventory model")
                                     : "lives in its room's geometry",
               kr->state == KEY_COPYING ? (s_map->keys[k].swap       ? "copying it, then moving to another item spot before the door it opens"
                                           : s_map->keys[k].anywhere ? "copying it, then moving to another item spot"
                                                                     : "copying it, then moving within its zone")
               : kr->cur == s_map->keys[k].slot ? "stays at its original spot"
               : kr->state == KEY_WIDE ? "moved within its zone" : "moved within its own cell");
        /* In plain view (screen up, player near either spot) it waits at home. */
        if (kr->state != KEY_COPYING)
            key_go(k, kr->state, kr->cur);
    }
}

/* Between areas (see below). */
static void    xmap_draw(void);
static int     s_xArrCount;
static uint8_t s_xGoneKey[REMIX_MAX_KEYS];
static uint8_t s_xGoneCommon[REMIX_MAX_PICKUPS];

/* ------------------------------------------------------------------ heights */

/* How far below its origin a model's lowest vertex sits (Q12; PSX y points
 * down, so an item lying on a surface has that surface at origin + bottom).
 * Mesh vertices are in the same Q8 units as the draw position. 0 = unknown. */
static int model_bottom(const s_ModelHeader* h, q19_12* out)
{
    int m, i, any = 0, best = -32768;

    if (h == NULL || h->meshHdrs == NULL || Pc_WideLm_IsWide(h))
        return 0;
    for (m = 0; m < h->meshCount; m++)
    {
        const s_MeshHeader* mh = &h->meshHdrs[m];
        for (i = 0; mh->verticesXy != NULL && i < mh->vertexCount; i++)
        {
            any = 1;
            if (mh->verticesXy[i].vy > best)
                best = mh->verticesXy[i].vy;
        }
    }
    /* More than 4 m off its origin is not a resting item. */
    if (!any || best > 1024 || best < -1024)
        return 0;
    *out = (q19_12)best * 16;
    return 1;
}

/* The model's bottom read from whichever loaded set holds it right now: the
 * global set, the item set, or a chunk that is fully in. Read at once and not
 * kept as a pointer, so a chunk going away later cannot matter. Its own
 * world object is not used: a moved item's is never resolved (the map's draw of
 * it is swallowed, and a plate is drawn with its inventory model instead). */
static int model_bottom_now(const s_WorldObjectModel* model, q19_12* out)
{
    s_WorldObjectModel tmp;
    int                i;

    if (model == NULL || model->metadata.name.str[0] == '\0')
        return 0;
    tmp = *model;
    tmp.metadata.lmIdx = 0;
    if (g_Map.globalLm.lmHdr != NULL && Fs_QueueEntryLoadStatusGet(g_Map.globalLm.queueIdx) >= ChunkLoadState_Loaded &&
        g_Map.globalLm.lmHdr->isLoaded && Lm_ModelFind(&tmp, g_Map.globalLm.lmHdr, &tmp.metadata))
        return model_bottom(tmp.modelInfo.modelHdr, out);
    if (item_lm_ready() && Lm_ModelFind(&tmp, &g_WorldGfxWork.itemLmHdr, &tmp.metadata))
        return model_bottom(tmp.modelInfo.modelHdr, out);
    for (i = 0; i < g_Map.activeChunkCount; i++)
    {
        s_Chunk* ch = &g_Map.activeChunks[i];
        if (!chunk_ready(ch))
            continue;
        tmp                = *model;
        tmp.metadata.lmIdx = 0;
        if (Lm_ModelFind(&tmp, ch->ipdHdr->lmHdr, &tmp.metadata))
            return model_bottom(tmp.modelInfo.modelHdr, out);
    }
    return 0;
}

/* A key's model bottom: from its own copy, or from whichever loaded set holds
 * its model. Unknown until one of them is in; known, it is kept. */
static int key_bottom(int k, q19_12* out)
{
    s_KeyRt* kr = &s_keys[k];

    if (kr->bottomKnown)
    {
        *out = kr->bottom;
        return 1;
    }
    if (kr->copy.built)
        kr->bottomKnown = model_bottom(&kr->copy.hdr, &kr->bottom);
    if (!kr->bottomKnown)
        kr->bottomKnown = model_bottom_now(kr->model, &kr->bottom);
    *out = kr->bottom;
    return kr->bottomKnown;
}

/* The Axe and the Hammer hang on a wall and stand against a cabinet: nothing
 * rests at their height, so what takes their spot lies on the floor below. */
static int item_on_wall(int item)
{
    return item == InvItemId_Axe || item == InvItemId_Hammer;
}

/* ------------------------------------------------------------------ pickup types */

/* Remix items: each health spot holds a health item and each ammo spot ammo, of a
 * type picked by (seed, the pickup's flag), weighted by remix_weight_* (by default
 * how often the vanilla game places each type). The pick is the same wherever the
 * pickup lies (another area, a moved spot), so its draw and its take always agree. */
static uint8_t s_typesOn;
static uint8_t s_commonType[REMIX_MAX_PICKUPS];
static uint8_t s_commonRetyped[REMIX_MAX_PICKUPS];

static int common_type_pick(int vanillaType, int flag)
{
    const int* WEIGHT = g_RemixCfg.remixWeights;
    uint32_t x;
    int      first, t, total = 0, r;

    if (!s_typesOn || vanillaType < 0 || vanillaType > 5)
        return vanillaType;
    first = vanillaType <= CommonPickupItemId_Ampoule ? 0 : 3;
    for (t = first; t < first + 3; t++)
        total += WEIGHT[t];
    if (total <= 0)
        return vanillaType;
    x  = s_seed * 0x9E3779B1u ^ (uint32_t)flag * 0x85EBCA6Bu ^ 0x7E57u;
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    r  = (int)(x % (uint32_t)total);
    for (t = first; t < first + 2 && r >= WEIGHT[t]; t++)
        r -= WEIGHT[t];
    return t;
}

u32 Pc_Remix_CommonType(u32 pickupType, s32 flag)
{
    if (!s_active)
        return pickupType;
    return (u32)common_type_pick((int)pickupType, flag);
}

static int common_bottom_of(int type, q19_12* out)
{
    const s_WorldObjectModel* mdl = &g_CommonWorldObjects[type];
    if (!item_lm_ready() || mdl->metadata.lmIdx == 0)
        return 0;
    return model_bottom(mdl->modelInfo.modelHdr, out);
}

static int common_bottom(int c, q19_12* out)
{
    const s_WorldObjectModel* mdl = &g_CommonWorldObjects[s_map->pickups[c].model];
    if (!item_lm_ready() || mdl->metadata.lmIdx == 0)
        return 0;
    return model_bottom(mdl->modelInfo.modelHdr, out);
}

/* The height of the surface the item at `slot` rests on in vanilla: the bottom
 * of that key's or common pickup's model as the map draws it, or the floor. */
static q19_12 slot_surface_y(int slot, q19_12 floorY)
{
    q19_12 b;
    int    j, c;

    for (j = 0; j < s_map->keyCount && j < REMIX_MAX_KEYS; j++)
        if (s_map->keys[j].slot == slot && s_keys[j].origPos != NULL)
        {
            if (item_on_wall(s_map->keys[j].item))
                return floorY;
            return s_keys[j].origPos->vy + (key_bottom(j, &b) ? b : 0);
        }
    c = s_map->keySlots[slot].common;
    if (c != 255 && s_pickupPose[c] != NULL)
        return s_pickupPose[c]->position.vy + (common_bottom(c, &b) ? b : 0);
    return floorY;
}

/* A moved key sits on the surface of its new spot, as high as the item that
 * was there. With its own model unread, it keeps its height above its own
 * spot's surface instead. */
static q19_12 key_dest_y(int k, q19_12 floorY)
{
    s_KeyRt* kr = &s_keys[k];
    q19_12   b, dest = slot_surface_y(kr->cur, floorY);

    if (key_bottom(k, &b))
        return dest - b;
    return kr->origPos->vy + dest - slot_surface_y(s_map->keys[k].slot, floorY);
}

static int key_moved(int k)
{
    return s_keys[k].enabled && s_keys[k].state != KEY_PENDING && s_keys[k].state != KEY_COPYING &&
           s_keys[k].state != KEY_WAITMOVE && s_keys[k].cur != s_map->keys[k].slot;
}

static s_WorldObjectModel* key_draw_model(int k)
{
    s_KeyCopy* c = &s_keys[k].copy;
    if (!c->built)
        return s_keys[k].model;
    /* Re-armed every frame: nothing may send it back to a chunk lookup. */
    c->obj.metadata.lmIdx     = 2;
    c->obj.modelInfo.modelHdr = &c->hdr;
    return &c->obj;
}

/* ------------------------------------------------------------------ plates */

/* The hospital's four plates are part of their rooms' geometry, and a copy of
 * that is only a fragment, posed for the room it came from: moved, it hangs in
 * the air at the room's angle. A moved plate is drawn with its inventory model
 * instead (ITEM/UNQ67-6A.TMD, as the take screen loads them), laid flat on its spot. Its texture is
 * ITEM/TIM03.TIM, which every hospital area keeps in VRAM for the inventory
 * (InvItemLoadFlag_Tex3); anywhere else it is not drawn this way. */
#define REMIX_PLATE_FIRST   InvItemId_PlateOfTurtle
#define REMIX_PLATE_PLATES  4
/* Then the weapons whose world models are posed for their home spot (the Axe
 * and Hammer hang on walls; the Handgun has none but the one Harry holds):
 * moved, they lie on their side with their inventory models instead. Those
 * are textured from ITEM/TIM00.TIM and TIM07.TIM, which every area keeps in
 * VRAM, so they draw anywhere; a model using any other page keeps the old way. */
#define REMIX_PLATE_COUNT   8
#define REMIX_PLATE_LIFT    1                 /* Q8: just clear of the surface under it */
#define REMIX_PLATE_DRAWS   32

extern s_WorldEnvWork g_WorldEnvWork;
extern void           GsSetLsMatrix(MATRIX* mp);

typedef struct {
    u8*               data;
    struct TMD_STRUCT obj;
    GsDOBJ2           dobj;
    MATRIX            flat;      /* lays it down at its size; rotation only */
    SVECTOR           centre;    /* of its bounding box, in its own units */
    s32               halfThick; /* Q8 */
    u32               pages;     /* texture pages its textured faces use, a bit each */
    uint8_t           state;     /* 0 unread, 1 ready, 2 unusable */
} s_RemixPlate;

typedef struct {
    int     plate;
    VECTOR3 pos; /* q19_12: centre x/z, the surface y it lies on */
    s16     yaw;
} s_RemixPlateDraw;

static s_RemixPlate     s_plates[REMIX_PLATE_COUNT];
static s_RemixPlateDraw s_plateDraws[REMIX_PLATE_DRAWS];
static int              s_plateDrawCount;
static uint8_t          s_plateLogged; /* plates whose first draw on this map was logged */

static int plate_of_item(int item)
{
    if (item >= REMIX_PLATE_FIRST && item < REMIX_PLATE_FIRST + REMIX_PLATE_PLATES)
        return item - REMIX_PLATE_FIRST;
    switch (item)
    {
        case InvItemId_Handgun:      return 4;
        case InvItemId_Axe:          return 5;
        case InvItemId_Hammer:       return 6;
        case InvItemId_HyperBlaster: return 7;
    }
    return -1;
}

/* Where a TMD packet keeps its first three vertex indices, by the layout
 * GsSortObject4J (libgs_stub.c) reads it with. -1 = not a layout it draws. */
static int tmd_tri_offsets(const u8* pp, int off[3])
{
    static const int8_t LIT[8][3] = {
        { 10, 12, 14 }, { 10, 14, 18 }, { 10, 12, 14 }, { 10, 14, 18 },  /* F3 G3 F4 G4 */
        { 18, 20, 22 }, { 18, 22, 26 }, { 22, 24, 26 }, { 22, 26, 30 },  /* TF3 TG3 TF4 TG4 */
    };
    static const int8_t UNLIT[8][3] = {
        { 8, 10, 12 },  { 16, 18, 20 }, { 8, 10, 12 },  { 20, 22, 24 },  /* NF3 NG3 NF4 NG4 */
        { 20, 22, 24 }, { 18, 22, 26 }, { 24, 26, 28 }, { 36, 38, 40 },  /* NTF3 NTG3 NTF4 NTG4 */
    };
    int mode = pp[3], kind = (((mode >> 2) & 1) << 2) | (((mode >> 3) & 1) << 1) | ((mode >> 4) & 1);
    const int8_t* o = (pp[2] & 1) ? LIT[kind] : UNLIT[kind];
    int i;

    for (i = 0; i < 3; i++)
    {
        off[i] = o[i];
        if (off[i] + 2 > (1 + pp[1]) * 4)
            return -1;
    }
    return 0;
}

/* Which way along its thin axis the plate's drawn faces look: a face is drawn
 * when its winding is clockwise on screen (NormalClip > 0), i.e. its outward
 * side is -(v1 - v0) x (v2 - v0). +1, -1, or 0 if both sides are alike. */
static int plate_face_side(const s_RemixPlate* pl, size_t primEnd, int axis)
{
    const SVECTOR* v  = (const SVECTOR*)pl->obj.vertop;
    const u8*      pp = (const u8*)pl->obj.primtop;
    const u8*      end = (const u8*)pl->obj.primtop + primEnd;
    double         pos = 0.0, neg = 0.0;
    unsigned long  n;

    for (n = 0; n < pl->obj.primn && pp + 4 <= end && pp[1] != 0; n++, pp += (1 + pp[1]) * 4)
    {
        int    off[3], i;
        u16    idx[3];
        double a[3], b[3], c;

        if (pp + (1 + pp[1]) * 4 > end || tmd_tri_offsets(pp, off) < 0)
            break;
        for (i = 0; i < 3; i++)
        {
            memcpy(&idx[i], pp + off[i], 2);
            if (idx[i] >= pl->obj.vern)
                break;
        }
        if (i < 3)
            continue;
        a[0] = v[idx[1]].vx - v[idx[0]].vx; a[1] = v[idx[1]].vy - v[idx[0]].vy; a[2] = v[idx[1]].vz - v[idx[0]].vz;
        b[0] = v[idx[2]].vx - v[idx[0]].vx; b[1] = v[idx[2]].vy - v[idx[0]].vy; b[2] = v[idx[2]].vz - v[idx[0]].vz;
        c = -(a[(axis + 1) % 3] * b[(axis + 2) % 3] - a[(axis + 2) % 3] * b[(axis + 1) % 3]);
        if (c > 0)
            pos += c;
        else
            neg -= c;
    }
    if (pos > neg * 1.25)
        return 1;
    if (neg > pos * 1.25)
        return -1;
    return 0;
}

static u32 plate_pages(const s_RemixPlate* pl, size_t primEnd)
{
    const u8*     pp  = (const u8*)pl->obj.primtop;
    const u8*     end = (const u8*)pl->obj.primtop + primEnd;
    u32           pages = 0;
    unsigned long n;

    for (n = 0; n < pl->obj.primn && pp + 4 <= end && pp[1] != 0; n++, pp += (1 + pp[1]) * 4)
    {
        u16 tsb;
        if (pp + (1 + pp[1]) * 4 > end)
            break;
        if (!((pp[3] >> 2) & 1) || (1 + pp[1]) * 4 < 12)
            continue;
        memcpy(&tsb, pp + 10, 2);
        pages |= 1u << (tsb & 0x1F);
    }
    return pages;
}

static int plate_load(s_RemixPlate* pl, s32 fileIdx, s32 sizeQ8)
{
    static const int8_t AXIS_W[3][2][3] = {
        /* [thin axis][face side < 0, > 0]: the world axis each local axis goes to, +1-based and signed */
        { { 2, -1, 3 }, { -2, 1, 3 } },
        { { 1, 2, 3 },  { 1, -2, -3 } },
        { { 1, -3, 2 }, { 1, 3, -2 } },
    };
    u8*            buf;
    s32            size = Fs_GetFileSectorAlignedSize(fileIdx);
    u32            raw[7];
    size_t         tbl  = 12, avail;
    const SVECTOR* v;
    s32            lo[3] = { 32767, 32767, 32767 }, hi[3] = { -32768, -32768, -32768 }, ext[3], longest;
    s32            scale;
    int            i, axis, side;
    unsigned long  n;

    if (size <= (s32)(tbl + sizeof(raw)) || (buf = xmap_read("", 0, fileIdx)) == NULL)
        return 0;
    pl->data = (u8*)malloc((size_t)size);
    if (pl->data == NULL)
        return 0;
    memcpy(pl->data, buf, (size_t)size);
    if (((u32*)pl->data)[2] == 0)
        return 0;
    memcpy(raw, pl->data + tbl, sizeof(raw));
    avail = (size_t)size - tbl;
    if (raw[1] == 0 || raw[1] > 0x2000 || raw[5] == 0 || raw[5] > 0x2000 ||
        raw[0] + (size_t)raw[1] * 8 > avail || raw[2] > avail || raw[4] >= avail)
        return 0;

    pl->obj.vertop  = (unsigned long*)(pl->data + tbl + raw[0]);
    pl->obj.vern    = raw[1];
    pl->obj.nortop  = (unsigned long*)(pl->data + tbl + raw[2]);
    pl->obj.norn    = raw[3];
    pl->obj.primtop = (unsigned long*)(pl->data + tbl + raw[4]);
    pl->obj.primn   = raw[5];
    pl->obj.scale   = raw[6];
    memset(&pl->dobj, 0, sizeof(pl->dobj));
    pl->dobj.tmd = (unsigned long*)&pl->obj;

    v = (const SVECTOR*)pl->obj.vertop;
    for (n = 0; n < pl->obj.vern; n++)
    {
        s32 c[3] = { v[n].vx, v[n].vy, v[n].vz };
        for (i = 0; i < 3; i++)
        {
            if (c[i] < lo[i]) lo[i] = c[i];
            if (c[i] > hi[i]) hi[i] = c[i];
        }
    }
    for (i = 0; i < 3; i++)
        ext[i] = hi[i] - lo[i];
    axis    = ext[0] <= ext[1] && ext[0] <= ext[2] ? 0 : ext[1] <= ext[2] ? 1 : 2;
    longest = ext[0] > ext[1] ? (ext[0] > ext[2] ? ext[0] : ext[2]) : (ext[1] > ext[2] ? ext[1] : ext[2]);
    if (longest <= 0)
        return 0;
    scale = (s32)(((s64)sizeQ8 << 12) / longest);
    if (scale < 1 || scale > 32767)
        return 0;

    /* Face up whichever side it is drawn from. Alike from both sides: the
     * side the inventory shows it from (its camera looks down +Z), which for
     * a flat plate is the low end of its thin axis. */
    side = plate_face_side(pl, avail - raw[4], axis);
    if (side == 0)
        side = -1;
    memset(&pl->flat, 0, sizeof(pl->flat));
    for (i = 0; i < 3; i++)
    {
        int w = AXIS_W[axis][side > 0][i];
        pl->flat.m[(w < 0 ? -w : w) - 1][i] = (s16)(w < 0 ? -scale : scale);
    }
    pl->centre.vx = (s16)((lo[0] + hi[0]) / 2);
    pl->centre.vy = (s16)((lo[1] + hi[1]) / 2);
    pl->centre.vz = (s16)((lo[2] + hi[2]) / 2);
    pl->halfThick = (s32)(((s64)ext[axis] * scale + 8191) >> 13);
    pl->pages     = plate_pages(pl, avail - raw[4]);
    SH_LOG("[RANDOMIZER] inventory model %d: %lu verts, %lu prims, thin axis %c, face %s, scale %d/4096, pages 0x%X",
           (int)fileIdx, pl->obj.vern, pl->obj.primn, "XYZ"[axis], side > 0 ? "+" : "-", (int)scale,
           (unsigned)pl->pages);
    return 1;
}

/* Pages of ITEM/TIM07.TIM and TIM00.TIM (GameFs_MapItemsModelLoad, GameFs_Tim00TIMLoad). */
#define REMIX_PAGE_TIM07 13
#define REMIX_PAGE_TIM00 15

static s_RemixPlate* plate_get(int p)
{
    /* As GameFs_UniqueItemModelLoad picks them: not in item-id order. */
    static const s32 FILES[REMIX_PLATE_COUNT] = { FILE_ITEM_UNQ6A_TMD,   /* Turtle */
                                                  FILE_ITEM_UNQ67_TMD,   /* Hatter */
                                                  FILE_ITEM_UNQ68_TMD,   /* Cat */
                                                  FILE_ITEM_UNQ69_TMD,   /* Queen */
                                                  FILE_ITEM_UNQA0_TMD,   /* Handgun */
                                                  FILE_ITEM_UNQ84_TMD,   /* Axe */
                                                  FILE_ITEM_UNQ82_TMD,   /* Hammer */
                                                  FILE_ITEM_UNQA3_TMD }; /* Hyper Blaster */
    /* Its longest side lying down, Q8 world units: about the real thing's. */
    static const s32 SIZES[REMIX_PLATE_COUNT] = { Q12(0.25f) >> 4, Q12(0.25f) >> 4, Q12(0.25f) >> 4, Q12(0.25f) >> 4,
                                                  Q12(0.22f) >> 4, Q12(0.75f) >> 4, Q12(0.35f) >> 4, Q12(0.30f) >> 4 };
    u32           flags = g_SysWork.invItemLoadFlags;
    s_RemixPlate* pl;

    if (p < 0 || p >= REMIX_PLATE_COUNT || (p < REMIX_PLATE_PLATES && !(flags & InvItemLoadFlag_Tex3)))
        return NULL;
    pl = &s_plates[p];
    if (pl->state == 0)
    {
        pl->state = plate_load(pl, FILES[p], SIZES[p]) ? 1 : 2;
        if (pl->state == 1 && p >= REMIX_PLATE_PLATES &&
            (pl->pages & ~((1u << REMIX_PAGE_TIM07) | (1u << REMIX_PAGE_TIM00))) != 0)
        {
            SH_LOG("[RANDOMIZER] inventory model %d uses a texture page not always in VRAM (0x%X) -- keeps its world model",
                   (int)FILES[p], (unsigned)pl->pages);
            pl->state = 2;
        }
        if (pl->state == 2)
        {
            free(pl->data);
            pl->data = NULL;
            SH_LOG("[RANDOMIZER] inventory model %d not used -- that item keeps its world model", (int)FILES[p]);
        }
    }
    if (pl->state != 1)
        return NULL;
    /* Not while its texture file is still on its way into VRAM. */
    if (((pl->pages >> REMIX_PAGE_TIM07) & 1) && p >= REMIX_PLATE_PLATES && !(flags & InvItemLoadFlag_Model))
        return NULL;
    if (((pl->pages >> REMIX_PAGE_TIM00) & 1) && p >= REMIX_PLATE_PLATES && !(flags & InvItemLoadFlag_Tex0))
        return NULL;
    return pl;
}

static int plate_usable(int p)
{
    return plate_get(p) != NULL;
}

static int plate_draw_add(int p, q19_12 x, q19_12 surfaceY, q19_12 z, s16 yaw)
{
    s_RemixPlateDraw* d;

    if (plate_get(p) == NULL || s_plateDrawCount >= REMIX_PLATE_DRAWS)
        return 0;
    d         = &s_plateDraws[s_plateDrawCount++];
    d->plate  = p;
    d->pos.vx = x;
    d->pos.vy = surfaceY;
    d->pos.vz = z;
    d->yaw    = yaw;
    return 1;
}

/* Called from Gfx_WorldObjectsDraw, with the frame's camera, into the world's
 * ordering table at the world's depth scale (OT bucket = SZ >> 3, shift 1 for
 * GsSortObject4J), so the plate sorts and depth-tests among the room's own
 * polygons. */
void Pc_Remix_WorldDraw(void)
{
    extern int g_PcItemDimNum;
    extern int g_PcItemPreciseDepth;
    extern int g_PcTmdWorldOtz;
    int        i;

    for (i = 0; i < s_plateDrawCount; i++)
    {
        const s_RemixPlateDraw* d  = &s_plateDraws[i];
        s_RemixPlate*           pl = plate_get(d->plate);
        GsCOORDINATE2           coord;
        MATRIX                  world, view;
        s32                     c = Math_Cos(d->yaw), s = Math_Sin(d->yaw);
        int                     r, col, tint;

        if (pl == NULL)
            continue;
        memset(&coord, 0, sizeof(coord));
        for (r = 0; r < 3; r++)
            for (col = 0; col < 3; col++)
                coord.coord.m[r][col] = pl->flat.m[r][col];
        for (col = 0; col < 3; col++)
        {
            s32 x = pl->flat.m[0][col], z = pl->flat.m[2][col];
            coord.coord.m[0][col] = (s16)((x * c + z * s) >> 12);
            coord.coord.m[2][col] = (s16)((z * c - x * s) >> 12);
        }
        coord.coord.t[0] = d->pos.vx >> 4;
        coord.coord.t[1] = (d->pos.vy >> 4) - pl->halfThick - REMIX_PLATE_LIFT;
        coord.coord.t[2] = d->pos.vz >> 4;
        for (r = 0; r < 3; r++)
            coord.coord.t[r] -= (coord.coord.m[r][0] * pl->centre.vx + coord.coord.m[r][1] * pl->centre.vy +
                                 coord.coord.m[r][2] * pl->centre.vz) >> 12;

        Vw_CoordToWorldAndViewMatrices(&coord, &world, &view);
        GsSetLsMatrix(&view);
        pl->dobj.coord2 = &coord;

        /* Its polygons carry no lighting; the room's ambient tint darkens it
         * toward how the room around it reads, never so far it can't be found. */
        tint = g_WorldEnvWork.worldTintColor.r;
        if (g_WorldEnvWork.worldTintColor.g > tint) tint = g_WorldEnvWork.worldTintColor.g;
        if (g_WorldEnvWork.worldTintColor.b > tint) tint = g_WorldEnvWork.worldTintColor.b;
        g_PcItemDimNum       = tint * 2 < 160 ? 160 : tint * 2 > 256 ? 256 : tint * 2;
        g_PcItemPreciseDepth = 1;
        g_PcTmdWorldOtz      = 1;
        GsSortObject4J(&pl->dobj, &g_OrderingTable0[g_ActiveBufferIdx], 1, (unsigned long*)PSX_SCRATCH);
        g_PcTmdWorldOtz      = 0;
        g_PcItemPreciseDepth = 0;
        g_PcItemDimNum       = 256;
        pl->dobj.coord2      = NULL;
        if (!(s_plateLogged & (1u << d->plate)))
        {
            s_plateLogged |= (uint8_t)(1u << d->plate);
            SH_LOG("[RANDOMIZER] plate %d drawn with its inventory model at (%.2f, %.2f, %.2f), view depth %d",
                   d->plate, d->pos.vx / 4096.0, d->pos.vy / 4096.0, d->pos.vz / 4096.0, (int)view.t[2]);
        }
    }
    s_plateDrawCount = 0;
}

/* WorldGfx_ObjectAdd hook: while the map's own update runs, swallow its draw of
 * a moved key at the key's original pose. The map draws it with
 * &<symbol>.position, so the pointer itself identifies the placement. */
int Pc_Remix_ObjectAddFilter(const s_WorldObjectModel* model, const VECTOR3* pos)
{
    int k;
    if (!s_filterOn)
        return 0;
    for (k = 0; k < s_map->keyCount && k < REMIX_MAX_KEYS; k++)
        if ((key_moved(k) || s_xGoneKey[k]) && model == s_keys[k].model && pos == s_keys[k].origPos)
            return 1;
    return 0;
}

static void keys_draw(void)
{
    const VECTOR3* player = &g_SysWork.playerWork.player.position;
    int k, j;

    for (k = 0; k < s_map->keyCount; k++)
    {
        const s_RemixKey*  key = &s_map->keys[k];
        s_KeyRt*           kr = &s_keys[k];
        q19_12             sx = kr->dynX, sz = kr->dynZ;
        s_CollisionSurface surf;
        VECTOR3            pos;
        SVECTOR3           rot = { 0, 0, 0 };
        s_WorldObjectModel* drawn;
        int                visibleNow = 0, before;

        if (!key_moved(k) || Savegame_EventFlagGet(key->flag))
            continue;
        /* Shown only while one of its pickup rows could fire, as the map would. */
        for (j = 0; j < key->rowCount && !visibleNow; j++)
            visibleNow = key->rows[j].requiredEventFlag == 0 ||
                         Savegame_EventFlagGet(key->rows[j].requiredEventFlag);
        if (!visibleNow)
            continue;

        if (dist2(sx, sz, player->vx, player->vz) >
            (s64)REMIX_PICKUP_DRAW_DIST * REMIX_PICKUP_DRAW_DIST)
            continue;

        if (func_800808AC(sx, sz) == 0)
        {
            /* Collision for the spot is resident and still no floor: unreachable. */
            if (func_800426E4(sx, sz) != NULL &&
                dist2(sx, sz, player->vx, player->vz) <= (s64)REMIX_KEY_PROBE_DIST * REMIX_KEY_PROBE_DIST)
                key_fall_back(k, "no floor at its spot");
            continue;
        }
        Collision_SurfaceGet(&surf, sx, sz);

        pos.vx = sx;
        pos.vy = surf.groundHeight;
        pos.vz = sz;
        if (key->isPose && kr->rot != NULL)
            rot = *kr->rot;
        if (kr->dynPos != NULL)
        {
            pos = *kr->dynPos;
            rot = kr->dynRot != NULL ? *kr->dynRot : (SVECTOR3){ 0, 0, 0 };
            pos.vy = key_dest_y(k, surf.groundHeight);
        }

        if (plate_draw_add(plate_of_item(key->item), sx, slot_surface_y(kr->cur, surf.groundHeight), sz,
                           kr->dynRot != NULL ? kr->dynRot->vy : (s16)((sx ^ sz) & 0xFFF)))
            continue;

        drawn  = key_draw_model(k);
        before = g_WorldGfxWork.objectCount;
        WorldGfx_ObjectAdd(drawn, &pos, &rot);
        /* Not added, list not full, and no model resolved: it cannot draw here. */
        if (g_WorldGfxWork.objectCount == before &&
            before < (s32)ARRAY_SIZE(g_WorldGfxWork.objects) &&
            drawn->metadata.lmIdx == 0)
            key_fall_back(k, "its model does not draw at its spot");
    }
}

/* ------------------------------------------------------------------ draw */

/* Draw the moved pickups instead of the map's own placements. The map draws each
 * common pickup behind `if (!Savegame_EventFlagGet(flag))` gated on the chunk the
 * player stands in, which only makes sense for its original spot. So for the
 * length of the map's own update every untaken moved pickup reads as taken, and we
 * then draw it ourselves at its new spot. The flags are restored before anything
 * else can observe them. Moved keys are hidden by Pc_Remix_ObjectAddFilter
 * instead: their flags may drive story logic in the same update. */

static void Pc_Remix_UpdateWorldObjects(void)
{
    const s_RemixMap* m = s_map;
    uint8_t           hidden[REMIX_MAX_PICKUPS];
    VECTOR3*          player = &g_SysWork.playerWork.player.position;
    int               i;

    s_plateDrawCount = 0;
    if (s_keysOn)
    {
        keys_classify();
        keys_copy_update();
    }

    for (i = 0; i < m->pickupCount; i++)
    {
        s16 flag  = m->pickups[i].flag;
        hidden[i] = (s_commonMoved[i] || s_commonRetyped[i] || s_xGoneCommon[i]) && !Savegame_EventFlagGet(flag);
        if (hidden[i])
            Savegame_EventFlagSet(flag);
    }

    s_filterOn = 1;
    s_origUpdateWorldObjects();
    s_filterOn = 0;

    /* Our own draws can fall through to the item set (see item_lm_ready). Moved
     * pickups stay hidden for these first frames instead. */
    if (!item_lm_ready())
    {
        for (i = 0; i < m->pickupCount; i++)
            if (hidden[i])
                Savegame_EventFlagClear(m->pickups[i].flag);
        return;
    }

    for (i = 0; i < m->pickupCount; i++)
    {
        s_CollisionSurface surf;
        VECTOR3            pos;
        SVECTOR3           rot;
        q19_12             x = s_commonX[i], z = s_commonZ[i];

        if (!hidden[i])
            continue;
        Savegame_EventFlagClear(m->pickups[i].flag);
        if (s_xGoneCommon[i])
            continue;

        if (dist2(x, z, player->vx, player->vz) > (s64)REMIX_PICKUP_DRAW_DIST * REMIX_PICKUP_DRAW_DIST)
            continue;

        /* Only where there is floor -- also keeps it hidden until the spot's
         * collision is resident. */
        if (func_800808AC(x, z) == 0)
            continue;
        Collision_SurfaceGet(&surf, x, z);

        pos.vx = x;
        pos.vy = surf.groundHeight;
        pos.vz = z;
        rot.vx = 0;
        rot.vy = (s16)((x ^ z ^ (i * 0x5A7)) & 0xFFF);
        rot.vz = 0;
        if (s_commonPos[i] == NULL && s_commonOnSlot[i] != 255)
        {
            q19_12 b;
            pos.vy = slot_surface_y(s_commonOnSlot[i], surf.groundHeight) - (common_bottom_of(s_commonType[i], &b) ? b : 0);
        }
        if (s_commonPos[i] != NULL)
        {
            pos = *s_commonPos[i];
            rot = s_commonRot[i] != NULL ? *s_commonRot[i] : (SVECTOR3){ 0, 0, 0 };
        }
        WorldGfx_ObjectAdd(&g_CommonWorldObjects[s_commonType[i]], &pos, &rot);
    }

    if (s_keysOn)
        keys_draw();
    if (s_xArrCount > 0)
        xmap_draw();
}

/* ------------------------------------------------------------------ load */

static void common_move(int c, q19_12 x, q19_12 z, const VECTOR3* pos, const SVECTOR3* rot)
{
    s_commonMoved[c] = 1;
    s_commonPos[c]   = pos;
    s_commonRot[c]   = rot;
    s_commonX[c]     = x;
    s_commonZ[c]     = z;
    pickup_trigger(s_map->pickups[c].event,
                   s_points[s_map->pickups[c].poi].positionX != x || s_points[s_map->pickups[c].poi].positionZ != z);
    s_points[s_map->pickups[c].poi].positionX = x;
    s_points[s_map->pickups[c].poi].positionZ = z;
}


/* ------------------------------------------------------------------ enemies */

/* Remix enemies. Every regular monster spawn of an enclosed area (MapFlag_Interior:
 * the schools, the hospitals, Nowhere, ...) or of the streets (the town, central
 * Silent Hill and the resort, in both worlds), the sewers or the amusement park gets a monster type picked from that
 * kind of area's pool by (seed, map, spawn row) -- so a row is always the same
 * monster for a seed, and the save's per-row "killed" bits keep meaning what they
 * did. The pick happens in Game_NpcRoomInitSpawn as the row spawns, after the map
 * has resolved the row's own type (Nowhere sets its group types from code), so it
 * sees exactly what vanilla would spawn there.
 *
 * Only rows whose own monster and entry state are a plain room spawn are
 * rerolled; ambush and tougher variants, bosses and scripted actors spawn as in
 * vanilla. A foreign type comes from the global chara pool (model, anims) and
 * chara_global.dll (AI); one the pool can't supply is skipped for the next. */
typedef struct { s8 charaId; u8 steps[2]; } s_RemixEnemy;

/* Indoors: no rompers or hanged scratchers. */
static const s_RemixEnemy ENEMY_POOL_INDOORS[] = {
    { Chara_GreyChild,    { 3, 3 } },
    { Chara_Mumbler,      { 3, 3 } },
    { Chara_Creeper,      { 3, 3 } },
    /* Groaner_Init only stands the dog up when stateStep == 3. */
    { Chara_Groaner,      { 3, 3 } },
    /* 1 + palette + 3 * look + 9 * stance (PuppetNurse_Init); the hospital's own. */
    { Chara_PuppetNurse,  { 14, 18 } },
    { Chara_PuppetDoctor, { 10, 10 } },
};
/* The normal school: the indoor mix plus larval stalkers. */
static const s_RemixEnemy ENEMY_POOL_SCHOOL[] = {
    { Chara_GreyChild,     { 3, 3 } },
    { Chara_Mumbler,       { 3, 3 } },
    { Chara_Creeper,       { 3, 3 } },
    { Chara_Groaner,       { 3, 3 } },
    { Chara_PuppetNurse,   { 14, 18 } },
    { Chara_PuppetDoctor,  { 10, 10 } },
    { Chara_LarvalStalker, { 3, 3 } },
};
/* The Otherworld school and hospital: wormheads instead of groaners, as on the
 * game's own Otherworld streets. */
static const s_RemixEnemy ENEMY_POOL_OTHERWORLD_INDOORS[] = {
    { Chara_GreyChild,    { 3, 3 } },
    { Chara_Mumbler,      { 3, 3 } },
    { Chara_Creeper,      { 3, 3 } },
    { Chara_Wormhead,     { 3, 3 } },
    { Chara_PuppetNurse,  { 14, 18 } },
    { Chara_PuppetDoctor, { 10, 10 } },
};
/* Nowhere: stalkers instead of grey children and mumblers, and -- Otherworld --
 * wormheads instead of groaners. */
static const s_RemixEnemy ENEMY_POOL_NOWHERE[] = {
    { Chara_Stalker,      { 3, 3 } },
    { Chara_Creeper,      { 3, 3 } },
    { Chara_Wormhead,     { 3, 3 } },
    { Chara_PuppetNurse,  { 14, 18 } },
    { Chara_PuppetDoctor, { 10, 10 } },
};
/* The streets. Air screamers hover (3) or glide in (6), the two the street maps
 * use most; in the Otherworld the night flutter and the wormhead stand in for the
 * air screamer and the groaner, as they do in the game's own Otherworld streets. */
static const s_RemixEnemy ENEMY_POOL_STREETS[] = {
    { Chara_AirScreamer,  { 3, 6 } },
    { Chara_Groaner,      { 3, 3 } },
    { Chara_Romper,       { 3, 3 } },
    { Chara_Creeper,      { 3, 3 } },
};
static const s_RemixEnemy ENEMY_POOL_OTHERWORLD_STREETS[] = {
    { Chara_NightFlutter, { 3, 6 } },
    { Chara_Wormhead,     { 3, 3 } },
    { Chara_Romper,       { 3, 3 } },
    { Chara_Creeper,      { 3, 3 } },
};

/* The sewers keep their hanged scratchers in the mix -- floor (6) or ceiling (9),
 * the two they use -- and the Otherworld sewer swaps groaners for wormheads.
 * A hanged scratcher's own spawn is never rerolled (enemy_rerollable). */
static const s_RemixEnemy ENEMY_POOL_SEWERS[] = {
    { Chara_Creeper,         { 3, 3 } },
    { Chara_HangedScratcher, { 6, 9 } },
    { Chara_Groaner,         { 3, 3 } },
    { Chara_Romper,          { 3, 3 } },
};
static const s_RemixEnemy ENEMY_POOL_OTHERWORLD_SEWERS[] = {
    { Chara_Creeper,         { 3, 3 } },
    { Chara_HangedScratcher, { 6, 9 } },
    { Chara_Mumbler,         { 3, 3 } },
    { Chara_Wormhead,        { 3, 3 } },
};
/* The amusement park (Otherworld only). */
static const s_RemixEnemy ENEMY_POOL_PARK[] = {
    { Chara_GreyChild,    { 3, 3 } },
    { Chara_Mumbler,      { 3, 3 } },
    { Chara_Romper,       { 3, 3 } },
    { Chara_Creeper,      { 3, 3 } },
    { Chara_Wormhead,     { 3, 3 } },
    { Chara_NightFlutter, { 3, 6 } },
    { Chara_LarvalStalker, { 3, 3 } },
};

/* Every normal monster the pools may hold, with its plain-spawn entry states, by
 * the name remix_enemies_<zone> lists it under. */
static const struct { const char* name; s_RemixEnemy e; } ENEMY_TYPES[] = {
    { "GreyChild",       { Chara_GreyChild,       { 3, 3 } } },
    { "Mumbler",         { Chara_Mumbler,         { 3, 3 } } },
    { "Creeper",         { Chara_Creeper,         { 3, 3 } } },
    { "Groaner",         { Chara_Groaner,         { 3, 3 } } },
    { "Wormhead",        { Chara_Wormhead,        { 3, 3 } } },
    { "Romper",          { Chara_Romper,          { 3, 3 } } },
    { "AirScreamer",     { Chara_AirScreamer,     { 3, 6 } } },
    { "NightFlutter",    { Chara_NightFlutter,    { 3, 6 } } },
    { "LarvalStalker",   { Chara_LarvalStalker,   { 3, 3 } } },
    { "Stalker",         { Chara_Stalker,         { 3, 3 } } },
    { "PuppetNurse",     { Chara_PuppetNurse,     { 14, 18 } } },
    { "PuppetDoctor",    { Chara_PuppetDoctor,    { 10, 10 } } },
    { "HangedScratcher", { Chara_HangedScratcher, { 6, 9 } } },
};

/* g_RemixCfg.remixEnemyPools[] index. */
enum
{
    ENEMY_ZONE_OUTSIDE,
    ENEMY_ZONE_OUTSIDE_OTHERWORLD,
    ENEMY_ZONE_SCHOOL,
    ENEMY_ZONE_OTHERWORLD_SCHOOL,
    ENEMY_ZONE_HOSPITAL,
    ENEMY_ZONE_BUILDINGS,
    ENEMY_ZONE_NOWHERE,
    ENEMY_ZONE_PARK,
    ENEMY_ZONE_SEWERS
};

static const s_RemixEnemy* s_enemyPool;
static int                 s_enemyPoolCount;
static int                 s_enemiesOn;
static int                 s_enemySewer;
static s_RemixEnemy        s_customPool[ARRAY_SIZE(ENEMY_TYPES)];

/* The zone's own list from config.cfg, if it has one: 1 with the pool set (it may be
 * empty: "none"), 0 for the built-in mix. */
static int enemy_custom_pool(int zone)
{
    const char* p = g_RemixCfg.remixEnemyPools[zone];
    int         n = 0;

    while (*p == ' ')
        p++;
    if (*p == '\0' || strncmp(p, "default", 7) == 0)
        return 0;
    while (*p != '\0')
    {
        const char* e = p;
        int         len, i;
        while (*e != '\0' && *e != ',')
            e++;
        len = (int)(e - p);
        while (len > 0 && p[len - 1] == ' ')
            len--;
        for (i = 0; i < (int)ARRAY_SIZE(ENEMY_TYPES); i++)
            if ((int)strlen(ENEMY_TYPES[i].name) == len && strncmp(ENEMY_TYPES[i].name, p, (size_t)len) == 0)
            {
                int j, dup = 0;
                for (j = 0; j < n; j++)
                    dup |= s_customPool[j].charaId == ENEMY_TYPES[i].e.charaId;
                if (!dup)
                    s_customPool[n++] = ENEMY_TYPES[i].e;
            }
        p = *e == ',' ? e + 1 : e;
        while (*p == ' ')
            p++;
    }
    s_enemyPool      = s_customPool;
    s_enemyPoolCount = n;
    return 1;
}

static int enemy_rerollable(s8 charaId, u8 step)
{
    switch (charaId)
    {
        case Chara_GreyChild:
        case Chara_Mumbler:
        case Chara_Creeper:
        case Chara_Romper:
        case Chara_Groaner:
        case Chara_Wormhead:
        case Chara_LarvalStalker:
        case Chara_Stalker:
            return step == 3;
        case Chara_AirScreamer:
        case Chara_NightFlutter:
            return step == 3 || step == 6;
        case Chara_PuppetNurse:
        case Chara_PuppetDoctor:
            return step != 0;
        /* The sewers' own hanged scratchers stay (other spawns there may still
         * become one) unless remix_enemies_sewer_scratchers asks otherwise. */
        case Chara_HangedScratcher:
            return s_enemySewer && g_RemixCfg.remixSewerScratchers && (step == 6 || step == 9);
        default:
            return 0;
    }
}

static int enemy_available(s8 charaId)
{
    return g_MapOverlayHdr.charaUpdateFuncs[charaId] != NULL &&
           g_WorldGfxWork.registeredCharaModels[charaId] != NULL &&
           g_CharaAnimDataIdxs[charaId] > 0;
}

static u32 enemy_hash(u32 seed, u32 mapIdx, u32 row)
{
    u32 h = seed * 0x9E3779B1u ^ (mapIdx + 1) * 0x85EBCA77u ^ (row + 1) * 0xC2B2AE3Du;
    /* murmur3 finalizer: every input bit reaches the low bits the pick uses */
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

void Pc_Remix_EnemyReroll(s32 row, s8* charaId, u8* stateStep)
{
    u32 h;
    int i, n = s_enemyPoolCount;

    if (!s_enemiesOn || n <= 0 || !enemy_rerollable(*charaId, *stateStep))
        return;

    h = enemy_hash(s_seed, (u32)g_SavegamePtr->mapIdx, (u32)row);
    /* The row's pick, or the next the pool can supply. */
    for (i = 0; i < n; i++)
    {
        const s_RemixEnemy* e = &s_enemyPool[(h + (u32)i) % (u32)n];
        if (e->charaId != *charaId && !enemy_available(e->charaId))
            continue;
        if (e->charaId == *charaId)
            return;
        SH_DBG("[RANDOMIZER] spawn row %d: chara %d -> %d", (int)row, (int)*charaId, (int)e->charaId);
        *charaId   = e->charaId;
        *stateStep = e->steps[(h >> 16) & 1];
        return;
    }
}

static void enemies_on_map_load(s32 mapIdx)
{
    const s_MapOverlayHdr* hdr = g_pMapOverlayHeader;
    const s_MapInfo*       mi;
    const char*            kind;
    int                    zone, custom;

    s_enemiesOn = 0;
    Pc_RemixSfx_Off();
    if (!g_RemixCfg.remixEnemies || hdr == NULL ||
        hdr->mapInfo == NULL || !map_selected(mapIdx))
        return;

    mi          = hdr->mapInfo;
    s_enemySewer = 0;
    if (mapIdx >= MapIdx_MAP7_S00 && mapIdx <= MapIdx_MAP7_S03)
    {
        s_enemyPool      = ENEMY_POOL_NOWHERE;
        s_enemyPoolCount = (int)ARRAY_SIZE(ENEMY_POOL_NOWHERE);
        kind             = "Nowhere";
        zone             = ENEMY_ZONE_NOWHERE;
    }
    else if (mi == &MAP_INFOS[MapType_SU] || mi == &MAP_INFOS[MapType_HU])
    {
        s_enemyPool      = ENEMY_POOL_OTHERWORLD_INDOORS;
        s_enemyPoolCount = (int)ARRAY_SIZE(ENEMY_POOL_OTHERWORLD_INDOORS);
        kind             = "Otherworld indoors";
        zone             = mi == &MAP_INFOS[MapType_SU] ? ENEMY_ZONE_OTHERWORLD_SCHOOL : ENEMY_ZONE_HOSPITAL;
    }
    else if (mi == &MAP_INFOS[MapType_SC])
    {
        s_enemyPool      = ENEMY_POOL_SCHOOL;
        s_enemyPoolCount = (int)ARRAY_SIZE(ENEMY_POOL_SCHOOL);
        kind             = "school";
        zone             = ENEMY_ZONE_SCHOOL;
    }
    else if ((mi->flags & MapFlag_Interior) || mi == &MAP_INFOS[MapType_HP])
    {
        s_enemyPool      = ENEMY_POOL_INDOORS;
        s_enemyPoolCount = (int)ARRAY_SIZE(ENEMY_POOL_INDOORS);
        kind             = "indoors";
        zone             = mi == &MAP_INFOS[MapType_HP] ? ENEMY_ZONE_HOSPITAL : ENEMY_ZONE_BUILDINGS;
    }
    else if (mi == &MAP_INFOS[MapType_THR] || mi == &MAP_INFOS[MapType_SPR] || mi == &MAP_INFOS[MapType_RSR])
    {
        s_enemyPool      = ENEMY_POOL_STREETS;
        s_enemyPoolCount = (int)ARRAY_SIZE(ENEMY_POOL_STREETS);
        kind             = "streets";
        zone             = ENEMY_ZONE_OUTSIDE;
    }
    else if (mi == &MAP_INFOS[MapType_SPU] || mi == &MAP_INFOS[MapType_RSU])
    {
        s_enemyPool      = ENEMY_POOL_OTHERWORLD_STREETS;
        s_enemyPoolCount = (int)ARRAY_SIZE(ENEMY_POOL_OTHERWORLD_STREETS);
        kind             = "Otherworld streets";
        zone             = ENEMY_ZONE_OUTSIDE_OTHERWORLD;
    }
    else if (mi == &MAP_INFOS[MapType_DR])
    {
        s_enemyPool      = ENEMY_POOL_SEWERS;
        s_enemyPoolCount = (int)ARRAY_SIZE(ENEMY_POOL_SEWERS);
        kind             = "sewers";
        zone             = ENEMY_ZONE_SEWERS;
    }
    else if (mi == &MAP_INFOS[MapType_DRU])
    {
        s_enemyPool      = ENEMY_POOL_OTHERWORLD_SEWERS;
        s_enemyPoolCount = (int)ARRAY_SIZE(ENEMY_POOL_OTHERWORLD_SEWERS);
        kind             = "Otherworld sewers";
        zone             = ENEMY_ZONE_SEWERS;
    }
    else if (mi == &MAP_INFOS[MapType_APR] || mi == &MAP_INFOS[MapType_APU])
    {
        s_enemyPool      = ENEMY_POOL_PARK;
        s_enemyPoolCount = (int)ARRAY_SIZE(ENEMY_POOL_PARK);
        kind             = "amusement park";
        zone             = ENEMY_ZONE_PARK;
    }
    else
        return;

    if (!g_PcConfig.globalCharaPool)
    {
        SH_LOG("[RANDOMIZER] enemies: needs global_chara_pool = 1 -- left vanilla");
        return;
    }
    custom       = enemy_custom_pool(zone);
    s_enemySewer = zone == ENEMY_ZONE_SEWERS;
    if (s_enemyPoolCount == 0)
    {
        SH_LOG("[RANDOMIZER] %s: no monsters picked for this kind of area -- spawns left vanilla",
               MapRegistry_GetName((e_MapIdx)mapIdx));
        return;
    }
    s_enemiesOn = 1;
    SH_LOG("[RANDOMIZER] %s (seed %llu): enemies remixed (%s%s, %d types)", MapRegistry_GetName((e_MapIdx)mapIdx),
           g_RemixCfg.remixSeed, kind, custom ? ", your list" : "", s_enemyPoolCount);

    /* Monsters from other areas bring their own sounds. */
    {
        s8  types[16];
        int i, n = 0;
        for (i = 0; i < s_enemyPoolCount && n < (int)ARRAY_SIZE(types); i++)
            types[n++] = s_enemyPool[i].charaId;
        Pc_RemixSfx_OnMapLoad(types, n, hdr->ambientAudioIdx);
    }
}

int Pc_Remix_AnyOn(void)
{
    return (g_RemixCfg.remix || g_RemixCfg.remixItems || g_RemixCfg.remixKeys || g_RemixCfg.remixEnemies ||
            g_RemixCfg.remixWeapons || g_RemixCfg.remixStarterWeapons || g_RemixCfg.remixUnlockables);
}

int Pc_Remix_EnemiesOn(void)
{
    return s_enemiesOn;
}

/* Remix switched off (or never on): drop whatever a previous remixed map left
 * behind -- key model copies pin textures -- so nothing of it survives. */
void Pc_Remix_Off(void)
{
    keys_release_all(1);
    memset(s_keys, 0, sizeof(s_keys));
    s_active    = 0;
    s_map       = NULL;
    s_keysOn    = 0;
    s_filterOn  = 0;
    s_enemiesOn = 0;
    Pc_RemixSfx_Off();
}


/* ------------------------------------------------------------------ between areas */

/* Items that trade places between two areas (REMIX_X_RULES): a key from one
 * area goes to a common pickup's spot in the other, and that pickup to the
 * key's spot. Both areas work the trades out from the seed alone, so each knows
 * what left it and what arrived without the other being loaded.
 *
 * What left: hidden (its draw filtered, or its flag held set while the map
 * draws) and its pickup rows' points moved out of reach. Its spot is blocked
 * for this area's own remix (g_RemixLayoutBlocked, the items' spot list).
 *
 * What arrived: a pickup row of its own (a copy of the row of the item that was
 * there, so it is picked up the same way), calling Pc_Remix_XTake, which hands
 * it to Event_CommonItemTake / Event_ItemTake as the item's own area would. A
 * common pickup is drawn from the item model set every area has. A key's model
 * only exists in its own area's files, so they are read into memory -- the IPD
 * of the cell its pose lies in (then the cells around it, then the area's global
 * model set) -- just long enough to copy the one model out (copy_build), with
 * its textures claimed into this area's pool and pinned. Nothing of the other
 * area is ever drawn or made resident. */

#define REMIX_X_MAX_TRADES 48
#define REMIX_X_MAX_ARR    16
#define REMIX_X_MAX_MSGS   256
#define REMIX_X_MAX_FUNCS  256
#define REMIX_X_BUILD_TRIES 3
#define REMIX_X_FAR        Q12(30000.0f)

typedef struct {
    const s_RemixXItem* out;  /* leaves its area... */
    const s_RemixXItem* in;   /* ...for this one's spot, which comes back to out's */
} s_RemixXTrade;

typedef struct {
    const s_RemixXItem* item;     /* what arrived */
    const s_RemixXItem* host;     /* whose spot it took here */
    int                 event;    /* its pickup row in s_events */
    int                 msg;      /* keys: its take message */
    const VECTOR3*      hostPos;  /* the host's pose here, if it has one */
    const SVECTOR3*     hostRot;
    s_KeyCopy           copy;
    int                 tries;
    int                 failed;
    q19_12              bottom;
    int                 bottomKnown;
} s_RemixXArr;

static s_RemixXTrade s_xTrades[REMIX_X_MAX_TRADES];
static int           s_xTradeCount;
static uint8_t       s_xGoneKey[REMIX_MAX_KEYS];
static uint8_t       s_xGoneCommon[REMIX_MAX_PICKUPS];
static uint8_t       s_xBlocked[REMIX_MAX_KEYSLOTS];
static s_RemixXArr   s_xArr[REMIX_X_MAX_ARR];
static const char*   s_xMsgs[REMIX_X_MAX_MSGS];
static void        (*s_xFuncs[REMIX_X_MAX_FUNCS])();

extern bool IpdHeader_FixOffsets_PC(s_IpdHeader* ipdHdr);
extern void LmHeader_FixOffsets(s_LmHeader* lmHdr);
extern int  Pc_MapMsgCount(int mapIdx);

static u32 xmap_rand(u32* st)
{
    *st ^= *st << 13;
    *st ^= *st >> 17;
    *st ^= *st << 5;
    return *st;
}

/* The trades for this seed. Keys only travel when their model can be copied
 * (resident textures); both areas must be remixed. */
/* ---------------------------------------------------------------- weapons */

static int16_t s_xHandgunFlag; /* the pickup flag that gives the Handgun instead, 0 = none */

/* REMIX_X_ITEMS' map for an item with no spot of its own (the Hyper Blaster):
 * it only ever arrives, and what it displaces is gone. */
#define REMIX_X_NOWHERE 255

#define REMIX_HANDGUN_MSG "\tThere_is_a_ ~C2 Handgun ~C7 . ~N\n\tTake_it? ~S4 "
/* The Handgun's model is the one Harry holds (ITEM/HANDGUN.PLM, textured from
 * HERO.TIM); lying down it gets these turns on top of a yaw of its own. */
#define REMIX_HANDGUN_ROT_X Q12_ANGLE(0.0f)
#define REMIX_HANDGUN_ROT_Z Q12_ANGLE(0.0f)

/* remix_weapons: the weapons found in the world move. */
static int world_weapons_on(void)
{
    return g_RemixCfg.remixWeapons;
}

/* remix_starter_weapons: the seed's starting gun. */
static int starter_on(void)
{
    return g_RemixCfg.remixStarterWeapons;
}

static int weapons_on(void)
{
    return world_weapons_on() || starter_on();
}

/* remix_unlockables: the next-fear bonus items move (REMIX_X_RULES weapons == 2). */
static int unlockables_on(void)
{
    return g_RemixCfg.remixUnlockables;
}

/* remix_unlockables_always: they lie in the world before they are unlocked. */
static int unlockables_always(void)
{
    return unlockables_on() && g_RemixCfg.remixUnlockablesAlways;
}

/* The Chainsaw's and Rock Drill's unlock while the Gasoline Tank is out of the
 * game. In the original the tank they need only appears in Next Fear (its row
 * requires EventFlag_62, set as Next Fear starts, func_800904F4), so that is
 * when they can first be taken -- or at once, with remix_unlockables_always. */
static int16_t fuel_unlock(void)
{
    return unlockables_always() ? 0 : EventFlag_62;
}

/* The door in central Silent Hill (map2_s00) to the Katana's room (map0_s02)
 * opens with a Next Fear extra (EventFlag_57, func_800904F4); before that its
 * locked row fires instead. With remix_unlockables_always it opens at once. */
static int katana_door_open(s32 mapIdx)
{
    return unlockables_always() && mapIdx == MapIdx_MAP2_S00;
}

static int is_unlockable(int item)
{
    return item == InvItemId_Katana || item == InvItemId_ChannelingStone || item == InvItemId_Chainsaw ||
           item == InvItemId_RockDrill || item == InvItemId_HyperBlaster;
}

/* Items remix_fixed_items keeps at their normal spots, by InvItemId. */
static uint8_t s_fixed[256];

static void fixed_items_parse(void)
{
    const char* p = g_RemixCfg.remixFixedItems;
    size_t      i;

    memset(s_fixed, 0, sizeof(s_fixed));
    while (*p)
    {
        const char* e;
        size_t      n;
        while (*p == ',' || *p == ' ')
            p++;
        for (e = p; *e && *e != ',' && *e != ' '; e++)
            ;
        n = (size_t)(e - p);
        for (i = 0; n > 0 && i < sizeof(REMIX_ITEM_NAMES) / sizeof(REMIX_ITEM_NAMES[0]); i++)
            if (strlen(REMIX_ITEM_NAMES[i].name) == n && strncmp(REMIX_ITEM_NAMES[i].name, p, n) == 0)
                s_fixed[REMIX_ITEM_NAMES[i].item] = 1;
        p = e;
    }
}

static int item_fixed(int item)
{
    return item >= 0 && item < (int)sizeof(s_fixed) && s_fixed[item];
}

/* With remix_unlockables the Gasoline Tank is taken out of the game and the
 * Chainsaw and Rock Drill need none -- unless both of them are kept at home,
 * when the game is as the original: the tank on its spot, the fuel needed. */
static int gas_removed(void)
{
    return unlockables_on() && !(item_fixed(InvItemId_Chainsaw) && item_fixed(InvItemId_RockDrill));
}

static int fuel_free(int item)
{
    return gas_removed() && (item == InvItemId_Chainsaw || item == InvItemId_RockDrill);
}

static int is_gas(const s_RemixXItem* it)
{
    return it->isKey && it->item == InvItemId_GasolineTank;
}

static int inv_has(int itemId)
{
    int i;
    for (i = 0; i < INV_ITEM_COUNT_MAX; i++)
        if (g_SavegamePtr->items[i].id_0 == itemId)
            return 1;
    return 0;
}

/* The save has the UFO ending: the game gives the Hyper Blaster from now on.
 * Only the save's part of Inventory_HyperBlasterUnlockTest: plugging in a gun
 * controller mid-game must not change the seed's starting gun or trades, so
 * that way of having it stays the game's own. */
static int hyper_unlocked(void)
{
    return g_SavegamePtr != NULL && g_SavegamePtr->clearGameCount != 0 &&
           (g_SavegamePtr->clearGameEndings & GameEndingFlag_Ufo);
}

/* Fixed by the seed: the Handgun, the Shotgun or the Hunting Rifle -- or,
 * once unlocked, the Hyper Blaster one seed in four. That is a roll of its own,
 * so the other seeds keep the gun they had before the unlock. */
static int start_gun(void)
{
    static const int GUNS[3] = { InvItemId_Handgun, InvItemId_Shotgun, InvItemId_HuntingRifle };
    u32 seed = RemixCfg_Seed32();
    u32 h = (seed ^ 0x6A09E667u) * 0x9E3779B1u;
    u32 x = (seed ^ 0xBB67AE85u) * 0x85EBCA6Bu;
    if (!starter_on())
        return InvItemId_Handgun;
    h ^= h >> 15;
    x ^= x >> 13;
    if (hyper_unlocked() && x % 4 == 0)
        return InvItemId_HyperBlaster;
    return GUNS[h % 3];
}

/* In the world this game: unlocked, or remix_unlockables_always. The starting
 * gun only ever takes a real unlock. */
static int hyper_available(void)
{
    return hyper_unlocked() || unlockables_always();
}

static int is_start_gun(int item)
{
    return item == start_gun() && item != InvItemId_Handgun;
}

static int gun_count(int itemId)
{
    switch (itemId)
    {
        case InvItemId_Handgun:      return HANDGUN_AMMO_PICKUP_ITEM_COUNT;
        case InvItemId_Shotgun:      return SHOTGUN_AMMO_PICKUP_ITEM_COUNT;
        case InvItemId_HuntingRifle: return RIFLE_AMMO_PICKUP_ITEM_COUNT;
        case InvItemId_HyperBlaster: return 1; /* as Inventory_AddSpecialItem gives it */
    }
    return DEFAULT_PICKUP_ITEM_COUNT;
}

/* A gun you already carry: its ammo instead (two of one gun would be two
 * inventory slots). 0 when it isn't such a gun. */
static int dup_gun_ammo(int itemId, int* msg)
{
    if (!inv_has(itemId))
        return 0;
    switch (itemId)
    {
        case InvItemId_Handgun:      if (msg) *msg = MapMsgIdx_HandgunAmmoSelect; return InvItemId_HandgunBullets;
        case InvItemId_Shotgun:      if (msg) *msg = MapMsgIdx_ShotgunAmmoSelect; return InvItemId_ShotgunShells;
        case InvItemId_HuntingRifle: if (msg) *msg = MapMsgIdx_RifleAmmoSelect;   return InvItemId_RifleShells;
    }
    return 0;
}

void Pc_Remix_RemapItemTake(s32* itemId, s32* itemCount, s32 eventFlagIdx, s32* mapMsgIdx)
{
    /* Event_ItemTake runs every frame of the take: decided once, at its start,
     * so the item never changes between showing it and adding it. */
    static s32 cFlag = -1, cItem, cCount, cMsg, cFrom;
    int        ammo, msg;

    if (!weapons_on())
        return;
    if (g_SysWork.sysStateSteps[0] == 0 || eventFlagIdx != cFlag || *itemId != cFrom)
    {
        cFlag  = eventFlagIdx;
        cFrom  = *itemId;
        cItem  = *itemId;
        cCount = *itemCount;
        cMsg   = *mapMsgIdx;
        if (s_xHandgunFlag != 0 && eventFlagIdx == s_xHandgunFlag)
        {
            cItem  = InvItemId_Handgun;
            cCount = HANDGUN_AMMO_PICKUP_ITEM_COUNT;
        }
        msg = cMsg;
        if ((ammo = dup_gun_ammo(cItem, &msg)) != 0)
        {
            cItem = ammo;
            cMsg  = msg;
        }
    }
    *itemId    = cItem;
    *itemCount = cCount;
    *mapMsgIdx = cMsg;
}

void Pc_Remix_RemapAddItem(s32* itemId, s32* itemCount)
{
    int ammo;

    if (!weapons_on())
        return;
    /* Cybil's handgun in the cafe: your starting gun instead. */
    if (starter_on() && *itemId == InvItemId_Handgun && g_SavegamePtr->mapIdx == MapIdx_MAP0_S01 &&
        !inv_has(InvItemId_Handgun) && !inv_has(InvItemId_Shotgun) && !inv_has(InvItemId_HuntingRifle) &&
        !inv_has(InvItemId_HyperBlaster))
    {
        *itemId    = start_gun();
        *itemCount = gun_count(*itemId);
        SH_LOG("[RANDOMIZER] weapons: starting gun is item %d", (int)*itemId);
        return;
    }
    if ((ammo = dup_gun_ammo(*itemId, NULL)) != 0)
        *itemId = ammo;
}

void Pc_Remix_StartLoadout(u8* gunId, u8* gunCount, u8* ammoId, u8* ammoCount)
{
    int g;
    if (!weapons_on())
        return;
    g = start_gun();
    if (g == InvItemId_Handgun)
        return;
    *gunId    = (u8)g;
    *gunCount = (u8)gun_count(g);
    /* It needs no ammo; the Handgun's stays, for the Handgun it finds. */
    if (g == InvItemId_HyperBlaster)
        return;
    *ammoId    = (u8)(g == InvItemId_Shotgun ? InvItemId_ShotgunShells : InvItemId_RifleShells);
    *ammoCount = (u8)gun_count(g);
}

/* One pickup, whichever rules list it (two pickups may share a flag: the
 * Shotgun lies in the Otherworld school or the police station). */
static u32 xmap_id(const s_RemixXItem* it)
{
    return ((u32)it->map << 16) | ((u32)it->isKey << 8) | it->local;
}

static int xmap_used(const u32* used, int n, const s_RemixXItem* it)
{
    int i;
    for (i = 0; i < n; i++)
        if (used[i] == xmap_id(it))
            return 1;
    return 0;
}

/* May `b` take `a`'s place: not traded yet, not `a` itself, and -- a key item --
 * moving with remix_keys and not kept at its spot. */
static int xmap_b_free(const u32* used, int nUsed, const s_RemixXItem* b, const s_RemixXItem* a)
{
    if (xmap_used(used, nUsed, b) || xmap_id(b) == xmap_id(a))
        return 0;
    /* The tank's spot, while the tank is out of the game. */
    if (is_gas(b))
        return gas_removed();
    if (b->isKey && (item_fixed(b->item) || !g_RemixCfg.remixKeys))
        return 0;
    return 1;
}

static void xmap_plan(void)
{
    u32     used[2 * REMIX_X_MAX_TRADES];
    int     nUsed = 0;
    u32     st = (s_seed * 0x9E3779B1u) ^ 0x51ED270Bu;
    int     r, i;

    s_xTradeCount  = 0;
    s_xHandgunFlag = 0;
    if (st == 0)
        st = 1;
    for (r = 0; r < REMIX_X_RULE_COUNT; r++)
    {
        const s_RemixXRule* rule = &REMIX_X_RULES[r];
        uint8_t             order[16];
        int                 n = rule->aCount < 16 ? rule->aCount : 16, keysInRule = 0;

        if (rule->aCount == 0 || rule->bCount == 0 || rule->den == 0)
            continue;
        /* A weapon's rule runs with remix_weapons; with only the starting gun on,
         * just for your starting gun's own pickup, so the Handgun that takes its
         * place still turns up somewhere. An unlockable's with remix_unlockables,
         * a key item's with remix_keys. */
        /* The Hyper Blaster's (3) with either, once unlocked (or with
         * remix_unlockables_always). */
        if (rule->weapons == 3 ? !(hyper_available() && (unlockables_on() || weapons_on()))
            : rule->weapons == 2 ? !unlockables_on() : rule->weapons ? !weapons_on() : !g_RemixCfg.remixKeys)
            continue;
        if ((REMIX_X_ITEMS[rule->aFirst].map != REMIX_X_NOWHERE && !map_selected(REMIX_X_ITEMS[rule->aFirst].map)) ||
            !map_selected(REMIX_X_ITEMS[rule->bFirst].map))
            continue;
        for (i = 0; i < n; i++)
            keysInRule |= REMIX_X_ITEMS[rule->aFirst + i].isKey;
        for (i = 0; i < rule->bCount; i++)
            keysInRule |= REMIX_X_ITEMS[rule->bFirst + i].isKey;
        if (keysInRule && !g_PcConfig.residentTextures)
            continue;
        for (i = 0; i < n; i++)
            order[i] = (uint8_t)i;
        for (i = n - 1; i > 0; i--)
        {
            int j = (int)(xmap_rand(&st) % (u32)(i + 1));
            uint8_t t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
        for (i = 0; i < n && s_xTradeCount < REMIX_X_MAX_TRADES; i++)
        {
            const s_RemixXItem* a = &REMIX_X_ITEMS[rule->aFirst + order[i]];
            int                 free = 0, pick, b;
            /* Rolled even when it can't trade, so one rule never shifts another's dice. */
            int                 roll = (int)(xmap_rand(&st) % rule->den) < rule->num;

            /* A kept item stays -- except your starting gun's pickup, which is
             * the Handgun now and has to turn up somewhere. */
            if (!roll || xmap_used(used, nUsed, a) ||
                (a->isKey && item_fixed(a->item) && !is_start_gun(a->item)))
                continue;
            if (rule->weapons == 1 && !world_weapons_on() && !(a->isKey && is_start_gun(a->item)))
                continue;
            if (rule->weapons == 3 && !world_weapons_on() && !unlockables_on() && !(a->isKey && is_start_gun(a->item)))
                continue;
            for (b = 0; b < rule->bCount; b++)
                free += xmap_b_free(used, nUsed, &REMIX_X_ITEMS[rule->bFirst + b], a);
            if (free == 0)
                continue;
            pick = (int)(xmap_rand(&st) % (u32)free);
            for (b = 0; b < rule->bCount; b++)
                if (xmap_b_free(used, nUsed, &REMIX_X_ITEMS[rule->bFirst + b], a) && pick-- == 0)
                    break;
            used[nUsed++] = xmap_id(a);
            used[nUsed++] = xmap_id(&REMIX_X_ITEMS[rule->bFirst + b]);
            s_xTrades[s_xTradeCount].out = a;
            s_xTrades[s_xTradeCount].in  = &REMIX_X_ITEMS[rule->bFirst + b];
            s_xTradeCount++;
            /* Not starting with the Handgun: your starting gun's own pickup is
             * the Handgun, wherever it went. */
            if (weapons_on() && a->isKey && is_start_gun(a->item) && s_xHandgunFlag == 0)
                s_xHandgunFlag = a->flag;
        }
    }
}

/* The inventory opening (item_screens_2.c): may the game hand over the Hyper
 * Blaster, as it does once unlocked? Not while Remix has it lying somewhere,
 * or has it as the starting gun: one Hyper Blaster, and never on top of
 * another starting gun. Worked out from the seed here, as the inventory opens
 * on maps Remix leaves alone too. */
int Pc_Remix_HyperBlasterGrant(void)
{
    int t;

    if (!hyper_available() || !(weapons_on() || unlockables_on()))
        return 1;
    if (start_gun() == InvItemId_HyperBlaster)
        return 0;
    s_seed = RemixCfg_Seed32();
    fixed_items_parse();
    xmap_plan();
    for (t = 0; t < s_xTradeCount; t++)
        if (s_xTrades[t].out->map == REMIX_X_NOWHERE && s_xTrades[t].out->item == InvItemId_HyperBlaster)
            return 0;
    return 1;
}

/* The Hyper Blaster fires as an unlocked one (item_screens_1.c), though the
 * save has no UFO ending: remix_unlockables_always put it in the world. */
int Pc_Remix_HyperBlasterWorks(void)
{
    return unlockables_always() && !item_fixed(InvItemId_HyperBlaster);
}

/* An arrival's required flag: an unlockable's unlock, which
 * remix_unlockables_always waives. */
static int16_t xmap_req(const s_RemixXItem* it)
{
    if (it->isKey && fuel_free(it->item))
        return fuel_unlock();
    if (unlockables_always() && it->isKey && is_unlockable(it->item))
        return 0;
    return it->reqFlag;
}

static const char* xmap_area_name(int mapIdx)
{
    return mapIdx == REMIX_X_NOWHERE ? "nowhere (it has no spot of its own)" : MapRegistry_GetName((e_MapIdx)mapIdx);
}

/* This area's side of the trades: what leaves it, and what arrives where. */
static void xmap_local(int mapIdx)
{
    int t, k;

    memset(s_xGoneKey, 0, sizeof(s_xGoneKey));
    memset(s_xGoneCommon, 0, sizeof(s_xGoneCommon));
    memset(s_xBlocked, 0, sizeof(s_xBlocked));
    s_xArrCount = 0;
    /* Out of the game: hidden and untakeable, whether or not anything took its spot. */
    if (gas_removed() && mapIdx >= 0 && mapIdx < REMIX_MAP_COUNT)
        for (k = 0; k < REMIX_MAPS[mapIdx].keyCount && k < REMIX_MAX_KEYS; k++)
            if (REMIX_MAPS[mapIdx].keys[k].item == InvItemId_GasolineTank)
            {
                s_xGoneKey[k] = 1;
                SH_LOG("[RANDOMIZER] the Gasoline Tank is out of the game (remix_unlockables)");
            }
    for (t = 0; t < s_xTradeCount; t++)
    {
        int side;
        for (side = 0; side < 2; side++)
        {
            const s_RemixXItem* here  = side ? s_xTrades[t].in : s_xTrades[t].out;
            const s_RemixXItem* there = side ? s_xTrades[t].out : s_xTrades[t].in;
            if (here->map != mapIdx || s_xArrCount >= REMIX_X_MAX_ARR)
                continue;
            if (here->isKey && here->local < REMIX_MAX_KEYS)
                s_xGoneKey[here->local] = 1;
            else if (!here->isKey && here->local < REMIX_MAX_PICKUPS)
                s_xGoneCommon[here->local] = 1;
            /* The tank is out of the game: nothing takes the mover's place. */
            if (is_gas(there))
            {
                SH_LOG("[RANDOMIZER] between areas: %s %d (flag %d) leaves for the Gasoline Tank's spot in %s",
                       here->isKey ? "key" : "pickup", here->local, here->flag, xmap_area_name(there->map));
                continue;
            }
            memset(&s_xArr[s_xArrCount], 0, sizeof(s_xArr[0]));
            s_xArr[s_xArrCount].item  = there;
            s_xArr[s_xArrCount].host  = here;
            s_xArr[s_xArrCount].event = -1;
            s_xArrCount++;
            SH_LOG("[RANDOMIZER] between areas: %s %d (flag %d) leaves for %s; %s %d (flag %d) comes here",
                   here->isKey ? "key" : "pickup", here->local, here->flag, xmap_area_name(there->map),
                   there->isKey ? "key" : "pickup", there->local, there->flag);
        }
    }
}

static void xmap_release(int mapGone)
{
    int i;
    for (i = 0; i < REMIX_X_MAX_ARR; i++)
        if (s_xArr[i].copy.built)
            copy_release(&s_xArr[i].copy, mapGone);
    s_xArrCount = 0;
}

/* A file of the other area, read into memory (padded: the IPD reformatter
 * checks offsets against the largest chunk buffer). One buffer, kept: the IPD
 * reformatter registers its collision by address, and that list never shrinks. */
static u8*    s_xBuf;
static size_t s_xBufSize;

static u8* xmap_read(const char* name, s32 type, s32 fileIdx)
{
    s32    idx = fileIdx >= 0 ? fileIdx : Fs_FindNextFile(name, type, 0);
    s32    size, q, n;
    size_t need;

    if (idx < 0)
        return NULL;
    size = Fs_GetFileSectorAlignedSize(idx);
    if (size <= 0 || size > 0x200000)
        return NULL;
    need = (size_t)(size > 0x2C000 ? size : 0x2C000) + 0x1000;
    if (need > s_xBufSize)
    {
        free(s_xBuf);
        s_xBuf     = (u8*)malloc(need);
        s_xBufSize = s_xBuf != NULL ? need : 0;
        if (s_xBuf == NULL)
            return NULL;
    }
    memset(s_xBuf, 0, s_xBufSize);
    q = Fs_QueueStartRead(idx, s_xBuf);
    for (n = 0; n < 4000 && Fs_QueueEntryLoadStatusGet(q) < ChunkLoadState_Loaded; n++)
        Fs_QueueUpdate();
    if (Fs_QueueEntryLoadStatusGet(q) < ChunkLoadState_Loaded)
    {
        SH_LOG("[RANDOMIZER] between areas: reading '%s' did not finish", name);
        s_xBuf     = NULL; /* the queue still owns it */
        s_xBufSize = 0;
        return NULL;
    }
    return s_xBuf;
}

/* Copy model `name` out of a model set read from another area. */
static int xmap_copy_from(s_KeyCopy* c, const char* name, s_LmHeader* lm, s_IpdHeader* ipd)
{
    s_WorldObjectModel tmp;
    uint8_t            claimed[256];
    int                i, ok;

    if (lm == NULL || lm->magic != LM_HEADER_MAGIC)
        return 0;
    memset(&tmp, 0, sizeof(tmp));
    memcpy(tmp.metadata.name.str, name, strlen(name) < 8 ? strlen(name) : 8);
    if (!Lm_ModelFind(&tmp, lm, &tmp.metadata))
        return 0;
    memset(claimed, 0, sizeof(claimed));
    for (i = 0; i < lm->materialCount && i < 256; i++)
        claimed[i] = lm->materials[i].texture == NULL;
    if (ipd != NULL)
        Ipd_MaterialsLoad(ipd, &g_Map.chunkTextures.fullPage, &g_Map.chunkTextures.halfPage, g_Map.textureFileIdx);
    else
        Lm_MaterialsLoadWithFilter(lm, &g_Map.chunkTextures.fullPage, NULL, g_Map.textureFileIdx, BlendMode_Additive);
    Lm_MaterialFlagsApply(lm);
    ok = copy_build(c, &tmp, tmp.modelInfo.modelHdr, lm);
    for (i = 0; i < lm->materialCount && i < 256; i++)
    {
        s_Texture* tex = lm->materials[i].texture;
        if (!claimed[i] || tex == NULL)
            continue;
        if (tex->refCount > 0)
            tex->refCount--;
        lm->materials[i].texture = NULL;
    }
    return ok;
}

/* The Handgun has no world model: the one Harry holds, textured as he holds
 * it, from the part of HERO.TIM that is always in VRAM. */
static int xmap_build_handgun(s_RemixXArr* a)
{
    s_FsImageDesc      desc;
    s_WorldObjectModel tmp;
    s_LmHeader*        lm;
    u8*                buf = xmap_read("ITEM/HANDGUN.PLM", 0, FILE_ITEM_HANDGUN_PLM);

    if (buf == NULL)
        return 0;
    lm = (s_LmHeader*)buf;
    LmHeader_FixOffsets(lm);
    if (lm->magic != LM_HEADER_MAGIC || lm->modelCount < 1)
        return 0;
    memset(&desc, 0, sizeof(desc));
    desc.tPage[1] = 27;
    desc.tPage[0] = 0;
    desc.u        = 0;
    desc.v        = 0;
    desc.clutX    = 736;
    desc.clutY    = 480;
    Pc_PlayAs_HeldItemImageDesc(&desc);
    Lm_MaterialFsImageApply1(lm, "HERO", &desc, BlendMode_Additive);
    Lm_MaterialFlagsApply(lm);
    memset(&tmp, 0, sizeof(tmp));
    tmp.metadata.name = lm->modelHdrs[0].name;
    if (!Lm_ModelFind(&tmp, lm, &tmp.metadata))
        return 0;
    if (!copy_build(&a->copy, &tmp, tmp.modelInfo.modelHdr, lm))
        return 0;
    SH_LOG("[RANDOMIZER] weapons: the Handgun's model read from ITEM/HANDGUN.PLM");
    return 1;
}

/* The arrived key's model: its own cell's IPD, the cells around it, then its
 * area's global model set. */
static int xmap_build(s_RemixXArr* a)
{
    const s_RemixXItem* it = a->item;
    int                 ring, dx, dz;

    if (it->isKey && s_xHandgunFlag != 0 && it->flag == s_xHandgunFlag)
        return xmap_build_handgun(a);
    /* No area files to copy it from: it only draws with its inventory model. */
    if (it->model == NULL || it->ipdTag == NULL)
        return 0;

    for (ring = 0; ring < 2; ring++)
        for (dz = -ring; dz <= ring; dz++)
            for (dx = -ring; dx <= ring; dx++)
            {
                char         name[16];
                u8*          buf;
                s_IpdHeader* ipd;
                int          ok;

                if ((dx < 0 ? -dx : dx) != ring && (dz < 0 ? -dz : dz) != ring)
                    continue;
                snprintf(name, sizeof(name), "%s%02X%02X", it->ipdTag, (unsigned)(u8)(it->cellX + dx),
                         (unsigned)(u8)(it->cellZ + dz));
                buf = xmap_read(name, FileType_Ipd, -1);
                if (buf == NULL)
                    continue;
                ipd = (s_IpdHeader*)buf;
                ok  = IpdHeader_FixOffsets_PC(ipd);
                if (ok)
                {
                    ipd->isLoaded = true;
                    LmHeader_FixOffsets(ipd->lmHdr);
                    ok = xmap_copy_from(&a->copy, it->model, ipd->lmHdr, ipd);
                    free(ipd->modelInfo);
                    free(ipd->modelBuffers);
                }
                if (ok)
                {
                    SH_LOG("[RANDOMIZER] between areas: model '%s' read from %s", it->model, name);
                    return 1;
                }
            }
    if (it->plmFile >= 0)
    {
        u8* buf = xmap_read("", 0, it->plmFile);
        if (buf != NULL)
        {
            s_LmHeader* lm = (s_LmHeader*)buf;
            int         ok;
            LmHeader_FixOffsets(lm);
            ok = xmap_copy_from(&a->copy, it->model, lm, NULL);
            if (ok)
            {
                SH_LOG("[RANDOMIZER] between areas: model '%s' read from its area's global model set", it->model);
                return 1;
            }
        }
    }
    return 0;
}

/* Called through the map's event table for an arrived item's pickup row. */
static void Pc_Remix_XTake(void)
{
    int i, row = (int)(g_MapEventData - s_events);

    for (i = 0; i < s_xArrCount; i++)
    {
        const s_RemixXItem* it = s_xArr[i].item;
        if (s_xArr[i].event != row)
            continue;
        if (it->isKey)
            Event_ItemTake((e_InvItemId)it->item, it->count ? it->count : DEFAULT_PICKUP_ITEM_COUNT,
                           (e_EventFlag)it->flag, s_xArr[i].msg);
        else
            Event_CommonItemTake(it->commonType, (e_EventFlag)it->flag);
        return;
    }
    SysWork_StateSetNext(SysState_Gameplay);
}

/* Where the item that was at the host's spot rested: its pose, or the floor. */
static q19_12 xmap_host_surface(const s_RemixXArr* a, q19_12 floorY)
{
    q19_12 b;
    if (a->hostPos == NULL)
        return floorY;
    if (!a->host->isKey && common_bottom(a->host->local, &b))
        return a->hostPos->vy + b;
    if (a->host->isKey)
    {
        if (item_on_wall(a->host->item))
            return floorY;
        if (a->host->local < s_map->keyCount && a->host->local < REMIX_MAX_KEYS && key_bottom(a->host->local, &b))
            return a->hostPos->vy + b;
    }
    return a->hostPos->vy;
}

static void xmap_draw(void)
{
    const VECTOR3* player = &g_SysWork.playerWork.player.position;
    int            i;

    for (i = 0; i < s_xArrCount; i++)
    {
        s_RemixXArr*        a  = &s_xArr[i];
        const s_RemixXItem* it = a->item;
        s_CollisionSurface  surf;
        VECTOR3             pos;
        SVECTOR3            rot = { 0, 0, 0 };
        q19_12              x = a->host->x, z = a->host->z, b;

        if (a->event < 0 || Savegame_EventFlagGet(it->flag) ||
            (xmap_req(it) != 0 && !Savegame_EventFlagGet(xmap_req(it))))
            continue;
        if (dist2(x, z, player->vx, player->vz) > (s64)REMIX_PICKUP_DRAW_DIST * REMIX_PICKUP_DRAW_DIST)
            continue;
        if (func_800808AC(x, z) == 0)
            continue;
        Collision_SurfaceGet(&surf, x, z);

        if (!it->isKey)
        {
            if (a->hostPos != NULL && !a->host->isKey)
            {
                pos = *a->hostPos;
                if (a->hostRot != NULL)
                    rot = *a->hostRot;
            }
            else
            {
                pos.vx = x;
                pos.vz = z;
                pos.vy = xmap_host_surface(a, surf.groundHeight);
                if (common_bottom_of(common_type_pick(it->commonType, it->flag), &b))
                    pos.vy -= b;
            }
            WorldGfx_ObjectAdd(&g_CommonWorldObjects[common_type_pick(it->commonType, it->flag)], &pos, &rot);
            continue;
        }

        if (plate_draw_add(plate_of_item(s_xHandgunFlag != 0 && it->flag == s_xHandgunFlag ? InvItemId_Handgun : it->item),
                           x, xmap_host_surface(a, surf.groundHeight), z,
                           a->hostRot != NULL ? a->hostRot->vy : (s16)((x ^ z) & 0xFFF)))
            continue;
        /* Drawn only with its inventory model, and that is not drawing now. */
        if (it->model == NULL && !(s_xHandgunFlag != 0 && it->flag == s_xHandgunFlag))
            continue;
        if (!a->copy.built)
        {
            if (a->failed || a->tries >= REMIX_X_BUILD_TRIES)
                continue;
            a->tries++;
            if (!xmap_build(a))
            {
                if (a->tries >= REMIX_X_BUILD_TRIES)
                {
                    a->failed = 1;
                    SH_LOG("[RANDOMIZER] between areas: model '%s' not found -- the item is there but not drawn",
                           it->model != NULL ? it->model : "(inventory model only)");
                }
                continue;
            }
            a->bottomKnown = model_bottom(&a->copy.hdr, &a->bottom);
        }
        if (!copy_textures_loaded(&a->copy))
            continue;
        pos.vx = it->poseX + (x - it->x);
        pos.vz = it->poseZ + (z - it->z);
        pos.vy = xmap_host_surface(a, surf.groundHeight) - (a->bottomKnown ? a->bottom : 0);
        rot.vx = it->rotX;
        rot.vy = it->rotY;
        rot.vz = it->rotZ;
        if (s_xHandgunFlag != 0 && it->flag == s_xHandgunFlag)
        {
            pos.vx = x;
            pos.vz = z;
            rot.vx = REMIX_HANDGUN_ROT_X;
            rot.vy = (s16)((x ^ z) & 0xFFF);
            rot.vz = REMIX_HANDGUN_ROT_Z;
        }
        a->copy.obj.metadata.lmIdx     = 2;
        a->copy.obj.modelInfo.modelHdr = &a->copy.hdr;
        WorldGfx_ObjectAdd(&a->copy.obj, &pos, &rot);
    }
}

/* The game answers the first row that matches, and a spot's remark ("Nothing
 * useful in the boxes.", "Nothing unusual." once its item is taken) is often
 * listed before the row of the item lying there: in vanilla that item's own row
 * comes first. An item moved onto such a spot would never be picked up, so the
 * remark moves to the end of the table (its own conditions kept), behind every
 * pickup: it answers once nothing else there does, as it did. */
static int s_shadowN;
static struct { q19_12 x, z; int row; } s_shadow[REMIX_MAX_KEYS * 2 * REMIX_KEY_ROWS + REMIX_MAX_PICKUPS + REMIX_X_MAX_ARR];

static void shadow_add(int row, q19_12 x, q19_12 z)
{
    if (row < 0 || s_shadowN >= (int)(sizeof(s_shadow) / sizeof(s_shadow[0])))
        return;
    s_shadow[s_shadowN].x   = x;
    s_shadow[s_shadowN].z   = z;
    s_shadow[s_shadowN].row = row;
    s_shadowN++;
}

static void messages_behind_pickups(const s_RemixMap* m, int nEvents, int* nextPoi, int* farPoi)
{
    int i, k, j, nRows, moved = 0;

    s_shadowN = 0;
    for (k = 0; k < m->keyCount && k < REMIX_MAX_KEYS; k++)
    {
        int s;
        if (!s_keys[k].enabled || s_xGoneKey[k])
            continue;
        for (s = 0; s < 2; s++)
        {
            int slot = s ? s_keys[k].wide : s_keys[k].cell;
            if (slot == m->keys[k].slot || slot >= m->keySlotCount)
                continue;
            for (j = 0; j < m->keys[k].rowCount; j++)
                shadow_add(m->keys[k].rows[j].event, m->keySlots[slot].x, m->keySlots[slot].z);
        }
    }
    for (i = 0; i < m->pickupCount && i < REMIX_MAX_PICKUPS; i++)
        if (s_commonMoved[i] && !s_xGoneCommon[i])
            shadow_add(m->pickups[i].event, s_commonX[i], s_commonZ[i]);
    for (i = 0; i < s_xArrCount; i++)
        shadow_add(s_xArr[i].event, s_xArr[i].host->x, s_xArr[i].host->z);
    if (s_shadowN == 0)
        return;

    for (nRows = 0; nRows < REMIX_MAX_EVENTS && s_events[nRows].triggerType != TriggerType_EndOfArray; nRows++) {}
    for (i = 0; i < nEvents; i++)
    {
        const s_MapPoint2d* p;
        int                 hit = 0;

        if (s_events[i].sysState != SysState_ReadMessage || s_events[i].pointOfInterestIdx >= m->pointCount)
            continue;
        p = &s_srcPoints[s_events[i].pointOfInterestIdx];
        for (j = 0; j < s_shadowN && !hit; j++)
            hit = s_shadow[j].row > i && ABS(p->positionX - s_shadow[j].x) <= Q12(1.0f) &&
                  ABS(p->positionZ - s_shadow[j].z) <= Q12(1.0f);
        if (!hit)
            continue;
        /* No room left in the table (Nowhere's is nearly full): the remark
         * waits for that one item instead. */
        if (nRows + 1 >= REMIX_MAX_EVENTS || (*farPoi < 0 && *nextPoi >= REMIX_MAX_POINTS))
        {
            for (j = 0; j < s_shadowN; j++)
                if (s_shadow[j].row > i && ABS(p->positionX - s_shadow[j].x) <= Q12(1.0f) &&
                    ABS(p->positionZ - s_shadow[j].z) <= Q12(1.0f))
                    s_events[i].requiredEventFlag = s_events[s_shadow[j].row].disabledEventFlag;
            continue;
        }
        if (*farPoi < 0)
        {
            *farPoi                      = (*nextPoi)++;
            s_points[*farPoi]            = s_srcPoints[0];
            s_points[*farPoi].positionX  = REMIX_X_FAR;
            s_points[*farPoi].positionZ  = REMIX_X_FAR;
        }
        s_events[nRows++]               = s_events[i];
        s_events[i].pointOfInterestIdx  = (u8)*farPoi;
        moved++;
    }
    s_events[nRows] = s_srcEvents[nEvents];
    if (moved > 0)
        SH_LOG("[RANDOMIZER] %d remark(s) moved behind the items now lying at their spots", moved);
}

void Pc_Remix_OnMapLoad(s32 mapIdx)
{
    const s_MapOverlayHdr* src;
    const s_RemixMap*      m;
    uint8_t                partner[REMIX_MAX_DOORS];
    uint8_t                reserved[REMIX_MAX_KEYSLOTS];
    uint8_t                wide[REMIX_MAX_KEYS], cell[REMIX_MAX_KEYS];
    int                    nEvents, i, k, zones = 0, movedCommons = 0, movedKeys = 0, retyped = 0;
    int                    rooms, items, keys, nextPoi, farPoi = -1, crossGroups = 0, crossDoors = 0, fuel, gone, katana;
    uint8_t                crossMap[REMIX_MAX_DOORS], crossDoor[REMIX_MAX_DOORS];

    keys_release_all(1);
    xmap_release(1);
    s_plateDrawCount     = 0;
    s_plateLogged        = 0;
    g_RemixLayoutBlocked = NULL;
    memset(s_xGoneKey, 0, sizeof(s_xGoneKey));
    memset(s_xGoneCommon, 0, sizeof(s_xGoneCommon));
    memset(s_keys, 0, sizeof(s_keys));
    s_seed = RemixCfg_Seed32();
    enemies_on_map_load(mapIdx);
    s_active   = 0;
    s_map      = NULL;
    s_keysOn   = 0;
    s_filterOn = 0;

    /* Rooms (`remix`), items (`remix_items`) and keys (`remix_keys`) are
     * independent; all share remix_seed and remix_maps. */
    if (!g_RemixCfg.remix && !g_RemixCfg.remixItems && !g_RemixCfg.remixKeys && !g_RemixCfg.remixWeapons &&
        !g_RemixCfg.remixStarterWeapons && !g_RemixCfg.remixUnlockables)
        return;
    s_seed = RemixCfg_Seed32();
    fixed_items_parse();
    if (mapIdx < 0 || mapIdx >= REMIX_MAP_COUNT)
        return;

    m = &REMIX_MAPS[mapIdx];
    if (!map_selected(mapIdx))
        return;

    src = g_pMapOverlayHeader;
    if (src == NULL || src->mapEvents == NULL || src->mapPoints == NULL)
        return;

    rooms = g_RemixCfg.remix && m->doorCount > 0;
    /* A pickup or key only moves if we can also move its model. */
    items = g_RemixCfg.remixItems && m->pickupCount > 0 && src->updateWorldObjects != NULL;
    keys  = g_RemixCfg.remixKeys && m->keyCount > 0 && src->updateWorldObjects != NULL;
    /* An area with no keys of its own may still trade with another (the
     * school's first floor takes the Chemical), so it is worked out first. */
    xmap_plan();
    xmap_local(mapIdx);
    for (k = 0, fuel = 0; k < m->keyCount && k < REMIX_MAX_KEYS; k++)
        fuel |= fuel_free(m->keys[k].item) || s_xGoneKey[k];
    katana = katana_door_open(mapIdx);
    if (!rooms && !items && !keys && s_xArrCount == 0 && !fuel && !katana)
        return;

    for (nEvents = 0; nEvents < REMIX_MAX_EVENTS; nEvents++)
        if (src->mapEvents[nEvents].triggerType == TriggerType_EndOfArray)
            break;
    if (nEvents >= REMIX_MAX_EVENTS)
        return;

    if (!tables_match(m, src->mapEvents, nEvents, src->mapPoints))
    {
        SH_LOG("[RANDOMIZER] %s: event table does not match pc_remix_data.h -- left vanilla "
               "(regenerate with pc_port/tools/gen_remix_data.py)",
               MapRegistry_GetName((e_MapIdx)mapIdx));
        return;
    }

    /* A bonus item whose room isn't unlocked yet stays where it is, and its
     * spot takes nothing else: the layout sees it pinned. So does a key that
     * left for another area (its spot holds what came from there). */
    {
        static s_RemixMap mapCopy;
        static s_RemixKey keysCopy[REMIX_MAX_KEYS];
        int               pin = 0;

        for (k = 0; k < m->keyCount && k < REMIX_MAX_KEYS; k++)
            pin |= (m->keys[k].unlockFlag != 0 && !Savegame_EventFlagGet(m->keys[k].unlockFlag)) || s_xGoneKey[k] ||
                   (item_fixed(m->keys[k].item) && !m->keys[k].pinned);
        if (pin)
        {
            mapCopy = *m;
            memcpy(keysCopy, m->keys, sizeof(s_RemixKey) * (m->keyCount < REMIX_MAX_KEYS ? m->keyCount : REMIX_MAX_KEYS));
            for (k = 0; k < m->keyCount && k < REMIX_MAX_KEYS; k++)
                if (s_xGoneKey[k])
                    keysCopy[k].pinned = 1;
                else if (item_fixed(keysCopy[k].item) && !keysCopy[k].pinned)
                {
                    keysCopy[k].pinned = 1;
                    SH_LOG("[RANDOMIZER] key %s: kept at its normal spot (remix_fixed_items)", keysCopy[k].symbol);
                }
                else if (keysCopy[k].unlockFlag != 0 && !Savegame_EventFlagGet(keysCopy[k].unlockFlag))
                {
                    keysCopy[k].pinned = 1;
                    SH_LOG("[RANDOMIZER] key %s: its room is still locked -> stays at its original spot",
                           keysCopy[k].symbol);
                }
            mapCopy.keys = keysCopy;
            m            = &mapCopy;
        }
    }

    memcpy(s_events, src->mapEvents, (size_t)(nEvents + 1) * sizeof(s_EventData));
    s_srcEvents = src->mapEvents;
    /* remix_unlockables: the Chainsaw and Rock Drill take no Gasoline Tank --
     * their pickup rows' required flag is the fuel having been used; it becomes
     * the tank's own unlock, Next Fear. */
    for (k = 0; fuel && k < m->keyCount && k < REMIX_MAX_KEYS; k++)
    {
        int j;
        if (!fuel_free(m->keys[k].item) || s_xGoneKey[k])
            continue;
        for (j = 0; j < m->keys[k].rowCount; j++)
            s_events[m->keys[k].rows[j].event].requiredEventFlag = fuel_unlock();
        SH_LOG("[RANDOMIZER] key %s: no Gasoline Tank needed (remix_unlockables)%s", m->keys[k].symbol,
               fuel_unlock() ? ", in Next Fear" : "");
    }
    for (i = 0; katana && i < nEvents; i++)
        if (s_events[i].requiredEventFlag == EventFlag_57 && s_events[i].sysState == SysState_LoadOverlay &&
            s_events[i].mapIdx == MapIdx_MAP0_S02)
        {
            s_events[i].requiredEventFlag = 0;
            SH_LOG("[RANDOMIZER] the door to the Katana's room is open (remix_unlockables_always)");
        }
    memcpy(s_points, src->mapPoints, (size_t)m->pointCount * sizeof(s_MapPoint2d));
    s_srcPoints = src->mapPoints;
    s_map = m;
    nextPoi = m->pointCount;

    /* Spots of pickups that left for another area hold what came from there. */
    for (i = 0; i < m->keySlotCount && i < REMIX_MAX_KEYSLOTS; i++)
        s_xBlocked[i] = m->keySlots[i].common != 255 && m->keySlots[i].common < REMIX_MAX_PICKUPS &&
                        s_xGoneCommon[m->keySlots[i].common];
    g_RemixLayoutBlocked = s_xBlocked;
    /* A point nobody reaches, for the pickup rows of what left (or was taken
     * out of the game with nothing arriving: the Gasoline Tank). */
    for (i = 0, gone = 0; i < REMIX_MAX_KEYS; i++)
        gone |= s_xGoneKey[i];
    for (i = 0; i < REMIX_MAX_PICKUPS; i++)
        gone |= s_xGoneCommon[i];
    if ((s_xArrCount > 0 || gone) && nextPoi < REMIX_MAX_POINTS)
    {
        farPoi                     = nextPoi++;
        s_points[farPoi]           = s_srcPoints[0];
        s_points[farPoi].positionX = REMIX_X_FAR;
        s_points[farPoi].positionZ = REMIX_X_FAR;
    }

    if (rooms)
        zones = RemixLayout_Doors(m, s_seed, mapIdx, partner);
    /* Rooms across floors: a zone in a group is rewired with the rest of its
     * building instead. Every map of the group works the same answer out from
     * the seed, so a door here and its partner on the other floor agree. */
    memset(crossMap, 255, sizeof(crossMap));
    for (i = 0; rooms && i < REMIX_DOOR_GROUP_COUNT; i++)
    {
        const s_RemixDoorGroup* g = &REMIX_DOOR_GROUPS[i];
        static s_RemixDoorRef   refs[REMIX_MAX_DOORS];
        static uint8_t          gp[REMIX_MAX_DOORS];
        int                     here = 0, all = 1, j, n;

        for (j = 0; j < g->memberCount; j++)
        {
            here |= g->members[j].map == mapIdx;
            all &= map_selected(g->members[j].map) && REMIX_DOOR_ARRIVALS[g->members[j].map] != NULL;
        }
        if (!here || !all || (n = RemixLayout_DoorGroup(g, REMIX_MAPS, s_seed, i, refs, gp)) == 0)
            continue;
        for (j = 0; j < n; j++)
        {
            const s_RemixDoorRef* p = &refs[gp[j]];
            if (refs[j].map != mapIdx)
                continue;
            if (p->map == mapIdx)
                partner[refs[j].door] = p->door;
            else
            {
                partner[refs[j].door]  = m->doors[refs[j].door].twin;
                crossMap[refs[j].door]  = p->map;
                crossDoor[refs[j].door] = p->door;
            }
        }
        crossGroups++;
    }
    for (i = 0; rooms && i < m->doorCount; i++)
    {
        const s_RemixDoor* land = &m->doors[m->doors[partner[i]].twin];
        s_EventData*       e    = &s_events[m->doors[i].event];

        if (crossMap[i] != 255)
        {
            /* Through this door: the other floor's map, arriving where its
             * partner door's twin would have brought you in (a point of that
             * map, copied here: a map load reads it from the map it leaves). */
            const s_RemixMap*  om = &REMIX_MAPS[crossMap[i]];
            int                lt = om->doors[crossDoor[i]].twin;
            if (nextPoi < REMIX_MAX_POINTS)
            {
                s_points[nextPoi] = REMIX_DOOR_ARRIVALS[crossMap[i]][lt];
                e->sysState       = SysState_LoadOverlay;
                e->eventParam     = (u32)nextPoi++;
                e->mapIdx         = crossMap[i];
                e->field_8_24     = om->doors[lt].rail;
                crossDoors++;
                continue;
            }
            land = &m->doors[m->doors[i].twin];   /* no room for the point: vanilla */
        }
        e->eventParam = land->ep;
        e->mapIdx     = land->bgm;
        e->field_8_24 = land->rail;
    }

    /* Keys: each needs its model, exported by the map DLL. A key stays at its
     * vanilla spot (state PENDING) until keys_classify decides, on the first
     * frames, whether its model draws anywhere (wide spot) or only in its own
     * cell (cell spot). */
    memset(reserved, 0, sizeof(reserved));
    memset(s_pickupPose, 0, sizeof(s_pickupPose));
    for (i = 0; i < m->pickupCount && i < REMIX_MAX_PICKUPS; i++)
        if (m->pickups[i].pose != NULL)
        {
            const s_Pose* pp = (const s_Pose*)MapOverlay_GetSymbol(m->pickups[i].pose);
            s_pickupPose[i]  = pp != NULL ? pp + m->pickups[i].poseIdx : NULL;
        }
    s_keyFrames = 0;
    if (keys)
    {
        int     any = 0, j;

        RemixLayout_Keys(m, s_seed, mapIdx, wide, cell, reserved);
        for (k = 0; k < m->keyCount && k < REMIX_MAX_KEYS; k++)
        {
            void* sym = MapOverlay_GetSymbol(m->keys[k].symbol);
            if (sym == NULL || m->keys[k].pinned || nextPoi + m->keys[k].rowCount > REMIX_MAX_POINTS)
                continue;
            /* Each pickup row gets a map point of its own, so only the pickup
             * travels: whatever else shares its vanilla point (an "already
             * taken" message, a "not unlocked yet" message) stays where it is. */
            for (j = 0; j < m->keys[k].rowCount; j++)
            {
                s_points[nextPoi]                                    = s_points[m->keys[k].rows[j].poi];
                s_events[m->keys[k].rows[j].event].pointOfInterestIdx = (u8)nextPoi;
                s_keys[k].poi[j]                                     = (uint8_t)nextPoi++;
            }
            sym = (u8*)sym + m->keys[k].index * (m->keys[k].isPose ? sizeof(s_RemixPose)
                                                                     : sizeof(s_WorldObjectPlacement));
            s_keys[k].model   = (s_WorldObjectModel*)sym;
            s_keys[k].origPos = &((s_WorldObjectPlacement*)sym)->position;
            s_keys[k].rot     = m->keys[k].isPose ? &((s_RemixPose*)sym)->rotation : NULL;
            s_keys[k].wide    = wide[k];
            s_keys[k].cell    = cell[k];
            s_keys[k].cur     = m->keys[k].slot;
            s_keys[k].dynX    = m->keySlots[m->keys[k].slot].x;
            s_keys[k].dynZ    = m->keySlots[m->keys[k].slot].z;
            s_keys[k].enabled = 1;
            any = 1;
            movedKeys += wide[k] != m->keys[k].slot || cell[k] != m->keys[k].slot;
        }
        keys = any;
    }
    s_keysOn = keys;

    /* Keys that left: their draw is filtered, their pickup rows lead nowhere. */
    for (k = 0; k < m->keyCount && k < REMIX_MAX_KEYS; k++)
    {
        void* sym;
        int   j;
        if (!s_xGoneKey[k] || (sym = MapOverlay_GetSymbol(m->keys[k].symbol)) == NULL)
            continue;
        sym = (u8*)sym + m->keys[k].index * (m->keys[k].isPose ? sizeof(s_RemixPose) : sizeof(s_WorldObjectPlacement));
        s_keys[k].model   = (s_WorldObjectModel*)sym;
        s_keys[k].origPos = &((s_WorldObjectPlacement*)sym)->position;
        s_keys[k].rot     = m->keys[k].isPose ? &((s_RemixPose*)sym)->rotation : NULL;
        for (j = 0; farPoi >= 0 && j < m->keys[k].rowCount; j++)
            s_events[m->keys[k].rows[j].event].pointOfInterestIdx = (u8)farPoi;
    }
    for (i = 0; farPoi >= 0 && i < m->pickupCount && i < REMIX_MAX_PICKUPS; i++)
        if (s_xGoneCommon[i])
            s_events[m->pickups[i].event].pointOfInterestIdx = (u8)farPoi;

    /* Common pickups: their own remix, then -- with keys on -- off any spot a key
     * may occupy. */
    memset(s_commonMoved, 0, sizeof(s_commonMoved));
    memset(s_commonRetyped, 0, sizeof(s_commonRetyped));
    memset(s_commonOnSlot, 255, sizeof(s_commonOnSlot));
    s_typesOn = (uint8_t)items;
    if (items || keys)
    {
        uint8_t spot[REMIX_MAX_PICKUPS];
        uint8_t exclude[REMIX_MAX_SPOTS], held[REMIX_MAX_KEYSLOTS], spotSlot[REMIX_MAX_SPOTS];
        static s_RemixSpot spotsX[REMIX_MAX_SPOTS];
        s_RemixMap         mx = *m;
        int     sIdx, attempt, keepHome = 0, nSpots = m->spotCount < REMIX_MAX_SPOTS ? m->spotCount : REMIX_MAX_SPOTS;

        /* Slots a key will occupy: where it goes, or its own spot while it
         * stays. A key's own spot it leaves is free. (Its fallback spot is
         * only used if its model can't be shown where it goes; then it may
         * share that spot with an item.) */
        /* If the pickups can't all get a spot of their own (a key took more of
         * their spots than the keys left free -- two keys can share one home),
         * a key that took a pickup's spot stays home instead, and we try again. */
        for (attempt = 0;; attempt++)
        {
        int unplaced = 0;

        nSpots = m->spotCount < REMIX_MAX_SPOTS ? m->spotCount : REMIX_MAX_SPOTS;
        memset(held, 0, sizeof(held));
        for (k = 0; k < m->keyCount && k < REMIX_MAX_KEYS; k++)
        {
            if (!s_keys[k].enabled)
            {
                if (m->keys[k].slot < m->keySlotCount)
                    held[m->keys[k].slot] = 1;
                continue;
            }
            if (keys)
                held[wide[k]] = 1;
        }
        for (i = 0; i < m->keySlotCount && i < REMIX_MAX_KEYSLOTS; i++)
            held[i] |= s_xBlocked[i];

        memcpy(spotsX, m->spots, sizeof(s_RemixSpot) * (size_t)nSpots);
        memset(spotSlot, 255, sizeof(spotSlot));
        /* Items may also go where a key that left lay, drawn on its surface. */
        for (i = 0; keys && i < m->keySlotCount && nSpots < REMIX_MAX_SPOTS; i++)
        {
            int owned = 0;
            for (k = 0; k < m->keyCount && k < REMIX_MAX_KEYS; k++)
                owned |= s_keys[k].enabled && m->keys[k].slot == i;
            if (!owned || held[i])
                continue;
            spotsX[nSpots].x    = m->keySlots[i].x;
            spotsX[nSpots].z    = m->keySlots[i].z;
            spotsX[nSpots].room = (uint8_t)(200 + (nSpots & 31));
            spotSlot[nSpots++]  = (uint8_t)i;
        }
        mx.spots     = spotsX;
        mx.spotCount = (uint8_t)nSpots;

        memset(exclude, 0, sizeof(exclude));
        for (i = 0; i < m->pickupCount && i < nSpots; i++)
            exclude[i] |= s_xGoneCommon[i];
        for (sIdx = 0; sIdx < nSpots; sIdx++)
            for (i = 0; i < m->keySlotCount; i++)
                /* The spot itself: two items the map lays side by side stay
                 * usable one at a time, as in vanilla. */
                if (held[i] && dist2(spotsX[sIdx].x, spotsX[sIdx].z,
                                     m->keySlots[i].x, m->keySlots[i].z) < (s64)Q12(0.25f) * Q12(0.25f))
                    exclude[sIdx] = 1;

        /* Items on: the pickups' own remix. Items off: each stays home unless a
         * key or an item from another area takes its place. Either way the
         * pass below then gives every pickup left without a spot a free one. */
        {
            if (items && !keepHome)
                RemixLayout_Pickups(&mx, s_seed, mapIdx, spot, exclude);
            else
                memset(spot, 255, sizeof(spot));
            for (i = 0; i < m->pickupCount; i++)
                if (s_xGoneCommon[i])
                    spot[i] = 255;
            /* A pickup left with no spot stays on its own, if that is still
             * free (no key coming, no other pickup sent there); otherwise it
             * takes any spot nobody has, or else the spot of a pickup whose own
             * place is free, which then stays home instead. */
            {
                uint8_t taken2[REMIX_MAX_SPOTS];
                int     d, t, pass2;

                memset(taken2, 0, sizeof(taken2));
                for (i = 0; i < m->pickupCount; i++)
                    if (spot[i] != 255)
                        taken2[spot[i]] = 1;
                for (pass2 = 0; pass2 < 2; pass2++)
                    for (i = 0; i < m->pickupCount && i < nSpots; i++)
                    {
                        if (spot[i] != 255 || s_xGoneCommon[i])
                            continue;
                        if (pass2 == 0)
                        {
                            if (!exclude[i] && !taken2[i])
                                spot[i] = (uint8_t)i, taken2[i] = 1;
                            continue;
                        }
                        for (t = 0; t < nSpots && spot[i] == 255; t++)
                            if (!exclude[t] && !taken2[t])
                                spot[i] = (uint8_t)t, taken2[t] = 1;
                        for (d = 0; d < m->pickupCount && d < nSpots && spot[i] == 255; d++)
                            if (spot[d] != 255 && spot[d] != d && !exclude[d] && !taken2[d] && !s_xGoneCommon[d])
                            {
                                spot[i] = spot[d];
                                spot[d] = (uint8_t)d;
                                taken2[d] = 1;
                            }
                    }
                for (i = 0; i < m->pickupCount && i < nSpots; i++)
                    unplaced |= spot[i] == 255 && !s_xGoneCommon[i];
                for (i = 0; i < m->pickupCount; i++)
                    if (spot[i] == i)
                        spot[i] = 255;
            }
        }
            if (!unplaced || (!keys && (keepHome || !items)))
                break;
            /* Prefer one that took a pickup's spot; else any (staying, it may
             * free the home another key that took a pickup's spot was bound for). */
            {
                int j, pref, homeWanted, pick = -1;
                for (pref = 1; pref >= 0 && pick < 0; pref--)
                    for (k = m->keyCount < REMIX_MAX_KEYS ? m->keyCount - 1 : REMIX_MAX_KEYS - 1; k >= 0 && pick < 0; k--)
                    {
                        if (!s_keys[k].enabled || wide[k] == m->keys[k].slot ||
                            (pref && m->keySlots[wide[k]].common == 255))
                            continue;
                        /* not one whose home another key is bound for */
                        for (homeWanted = 0, j = 0; j < m->keyCount && j < REMIX_MAX_KEYS; j++)
                            homeWanted |= j != k && s_keys[j].enabled && wide[j] == m->keys[k].slot &&
                                          m->keys[j].slot != m->keys[k].slot;
                        if (!homeWanted)
                            pick = k;
                    }
                k = pick;
            }
            /* Still short with every key home (an item from another area took a
             * spot): the pickups stay home instead of shuffling, so at worst two
             * lie as close as the map itself puts them. */
            if (k < 0)
            {
                if (keepHome || !items)
                    break;
                keepHome = 1;
                continue;
            }
            SH_LOG("[RANDOMIZER] key %s: stays home, so every pickup keeps a spot of its own", m->keys[k].symbol);
            wide[k]         = m->keys[k].slot;
            s_keys[k].wide  = m->keys[k].slot;
            movedKeys--;
        }
        {
            for (i = 0; i < m->pickupCount; i++)
                if (spot[i] != 255 && spot[i] < m->spotCount)
                    common_move(i, m->spots[spot[i]].x, m->spots[spot[i]].z,
                                s_pickupPose[spot[i]] ? &s_pickupPose[spot[i]]->position : NULL,
                                s_pickupPose[spot[i]] ? &s_pickupPose[spot[i]]->rotation : NULL);
                else if (spot[i] != 255)
                {
                    common_move(i, spotsX[spot[i]].x, spotsX[spot[i]].z, NULL, NULL);
                    take_place_trigger(m->pickups[i].event, &s_points[m->pickups[i].poi], spotSlot[spot[i]]);
                    s_commonOnSlot[i] = spotSlot[spot[i]];
                }
        }
        for (i = 0; i < m->pickupCount; i++)
            movedCommons += s_commonMoved[i];
    }

    /* What arrived from another area: a pickup row of its own, copied from the
     * row of the item that was at its spot. */
    if (s_xArrCount > 0)
    {
        int maxFunc = -1, msgCount = Pc_MapMsgCount(mapIdx), nRows = nEvents, a;

        for (i = 0; i < nEvents; i++)
            if (s_srcEvents[i].sysState == SysState_EventCallback && (int)s_srcEvents[i].eventParam > maxFunc)
                maxFunc = (int)s_srcEvents[i].eventParam;
        for (i = 0; i <= maxFunc && i < REMIX_X_MAX_FUNCS - 1; i++)
            s_xFuncs[i] = src->mapEventFuncs != NULL ? src->mapEventFuncs[i] : NULL;
        s_xFuncs[maxFunc + 1] = Pc_Remix_XTake;
        for (i = 0; i < msgCount && i < REMIX_X_MAX_MSGS - REMIX_X_MAX_ARR; i++)
            s_xMsgs[i] = src->mapMessages != NULL ? ((const char**)src->mapMessages)[i] : "";

        for (a = 0; a < s_xArrCount; a++)
        {
            s_RemixXArr*        ar   = &s_xArr[a];
            const s_RemixXItem* host = ar->host;
            int                 tRow, tPoi;

            if (host->isKey)
            {
                const s_RemixKey* hk  = &m->keys[host->local];
                void*             sym = MapOverlay_GetSymbol(hk->symbol);
                tRow = hk->rows[0].event;
                tPoi = hk->rows[0].poi;
                if (sym != NULL)
                {
                    sym         = (u8*)sym + hk->index * (hk->isPose ? sizeof(s_RemixPose) : sizeof(s_WorldObjectPlacement));
                    ar->hostPos = &((s_WorldObjectPlacement*)sym)->position;
                    ar->hostRot = hk->isPose ? &((s_RemixPose*)sym)->rotation : NULL;
                }
            }
            else
            {
                tRow = m->pickups[host->local].event;
                tPoi = m->pickups[host->local].poi;
                if (s_pickupPose[host->local] != NULL)
                {
                    ar->hostPos = &s_pickupPose[host->local]->position;
                    ar->hostRot = &s_pickupPose[host->local]->rotation;
                }
            }
            if (nRows + 1 >= REMIX_MAX_EVENTS || nextPoi >= REMIX_MAX_POINTS || maxFunc + 1 >= REMIX_X_MAX_FUNCS ||
                (ar->item->isKey && (msgCount <= 0 || msgCount + a >= REMIX_X_MAX_MSGS)))
            {
                SH_LOG("[RANDOMIZER] between areas: no room for an arrived item's pickup on this map");
                continue;
            }
            s_points[nextPoi]                = s_srcPoints[tPoi];
            s_points[nextPoi].positionX      = host->x;
            s_points[nextPoi].positionZ      = host->z;
            s_events[nRows]                  = s_srcEvents[tRow];
            s_events[nRows].sysState         = SysState_EventCallback;
            s_events[nRows].eventParam       = (u32)(maxFunc + 1);
            s_events[nRows].requiredEventFlag = xmap_req(ar->item);
            s_events[nRows].disabledEventFlag = ar->item->flag;
            s_events[nRows].pointOfInterestIdx = (u8)nextPoi++;
            ar->event = nRows++;
            if (ar->item->isKey)
            {
                ar->msg              = msgCount + a;
                s_xMsgs[ar->msg]     = (s_xHandgunFlag != 0 && ar->item->flag == s_xHandgunFlag) ? REMIX_HANDGUN_MSG
                                                                                                 : ar->item->takeMsg;
            }
        }
        s_events[nRows] = s_srcEvents[nEvents];
    }

    messages_behind_pickups(m, nEvents, &nextPoi, &farPoi);

    /* A pickup left on its own spot but of another type is drawn by us too,
     * with the map's own pose. */
    for (i = 0; i < m->pickupCount && i < REMIX_MAX_PICKUPS; i++)
    {
        s_commonType[i] = (uint8_t)common_type_pick(m->pickups[i].model, m->pickups[i].flag);
        if (s_commonType[i] == m->pickups[i].model || s_commonMoved[i] || s_xGoneCommon[i])
            continue;
        s_commonRetyped[i] = 1;
        s_commonPos[i]     = s_pickupPose[i] != NULL ? &s_pickupPose[i]->position : NULL;
        s_commonRot[i]     = s_pickupPose[i] != NULL ? &s_pickupPose[i]->rotation : NULL;
        s_commonX[i]       = s_srcPoints[m->pickups[i].poi].positionX;
        s_commonZ[i]       = s_srcPoints[m->pickups[i].poi].positionZ;
    }
    for (i = 0; i < m->pickupCount && i < REMIX_MAX_PICKUPS; i++)
        retyped += s_commonRetyped[i];

    s_origUpdateWorldObjects = (movedCommons || retyped || keys || s_xArrCount > 0 || gone) ? src->updateWorldObjects : NULL;

    s_hdr           = *src;
    s_hdr.mapEvents = s_events;
    s_hdr.mapPoints = s_points;
    if (s_xArrCount > 0)
    {
        s_hdr.mapEventFuncs = s_xFuncs;
        s_hdr.mapMessages   = (void*)s_xMsgs;
    }
    if (s_origUpdateWorldObjects != NULL)
        s_hdr.updateWorldObjects = Pc_Remix_UpdateWorldObjects;

    g_pMapOverlayHeader = &s_hdr;
    s_active            = 1;

    if (crossGroups)
        SH_LOG("[RANDOMIZER] %s: rooms across floors: %d door(s) here lead to another floor", MapRegistry_GetName((e_MapIdx)mapIdx),
               crossDoors);
    SH_LOG("[RANDOMIZER] %s (seed %llu): rooms %s (%d doors, %d of %d zone(s) rebuilt), "
           "items %s, keys %s (%d of %d may move); %d pickups moved",
           MapRegistry_GetName((e_MapIdx)mapIdx), g_RemixCfg.remixSeed,
           rooms ? "on" : "off", rooms ? m->doorCount : 0, zones, m->zoneCount,
           items ? "on" : "off", keys ? "on" : "off", movedKeys, m->keyCount, movedCommons);
    g_RemixLayoutBlocked = NULL;
}

void Pc_Remix_Init(void)
{
    if (g_RemixCfg.remix || g_RemixCfg.remixItems || g_RemixCfg.remixKeys || g_RemixCfg.remixEnemies)
        SH_LOG("[RANDOMIZER] rooms %s, items %s, keys %s, enemies %s, seed %llu, maps: %s",
               g_RemixCfg.remix ? "on" : "off", g_RemixCfg.remixItems ? "on" : "off",
               g_RemixCfg.remixKeys ? "on" : "off", g_RemixCfg.remixEnemies ? "on" : "off",
               g_RemixCfg.remixSeed, g_RemixCfg.remixMaps[0] ? g_RemixCfg.remixMaps : "all");
}
