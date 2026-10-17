/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Remix mode: room-graph and pickup layout. Pure functions over the generated
 * tables in pc_remix_data.h -- no game state -- so the same code runs in the game
 * and in pc_port/tests/remix_layout_test.c. See docs/Remix_Mode.md. */

#include "pc_remix_layout.h"

#include <string.h>

typedef struct { uint32_t s; } s_Rng;

static uint32_t rng_next(s_Rng* r)
{
    r->s ^= r->s << 13;
    r->s ^= r->s >> 17;
    r->s ^= r->s << 5;
    return r->s;
}

static int rng_below(s_Rng* r, int n) { return (n <= 0) ? 0 : (int)(rng_next(r) % (uint32_t)n); }

static void rng_seed(s_Rng* r, uint32_t seed, int mapIdx, uint32_t stream)
{
    /* splitmix32-style finalizer: nearby seeds and maps give unrelated streams */
    uint32_t x = seed * 0x9E3779B9u ^ (uint32_t)(mapIdx + 1) * 0x85EBCA6Bu ^ stream;
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    r->s = x ? x : 0x6D2B79F5u;
}

static void shuffle_u8(s_Rng* r, uint8_t* a, int n)
{
    int i;
    for (i = n - 1; i > 0; i--)
    {
        int j = rng_below(r, i + 1);
        uint8_t t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

static void link(uint8_t* partner, int a, int b)
{
    partner[a] = (uint8_t)b;
    partner[b] = (uint8_t)a;
}

static uint8_t m_room(const s_RemixMap* maps, const s_RemixDoorRef* refs, int i)
{
    return maps[refs[i].map].doors[refs[i].door].room;
}

/* Pair the leftover stubs of one attempt. No door may lead back into its own
 * room; a repeated room pair is only accepted if nothing else fits. */
static int pair_rest(s_Rng* r, const s_RemixDoor* d, uint8_t* stubs, int n, uint8_t* partner)
{
    int strict, tries, i, j;

    if (n == 0)
        return 1;

    for (strict = 1; strict >= 0; strict--)
    {
        for (tries = 0; tries < 48; tries++)
        {
            int ok = 1;
            shuffle_u8(r, stubs, n);
            for (i = 0; i < n && ok; i += 2)
            {
                if (d[stubs[i]].room == d[stubs[i + 1]].room)
                    ok = 0;
                for (j = 0; j < i && ok && strict; j += 2)
                {
                    int a0 = d[stubs[i]].room, a1 = d[stubs[i + 1]].room;
                    int b0 = d[stubs[j]].room, b1 = d[stubs[j + 1]].room;
                    if ((a0 == b0 && a1 == b1) || (a0 == b1 && a1 == b0))
                        ok = 0;
                }
            }
            if (ok)
            {
                for (i = 0; i < n; i += 2)
                    link(partner, stubs[i], stubs[i + 1]);
                return 1;
            }
        }
    }
    return 0;
}

/* One attempt at rebuilding a zone: grow a random spanning tree room by room
 * (each new room spends one of its doors on a door already in the tree), then pair
 * whatever doors are left. Rooms with several doors are preferred while only one
 * tree door is open, so the tree can never run out of doors to attach to. */
static int build_zone(s_Rng* r, const s_RemixDoor* d, const uint8_t* zoneDoors, int nz, uint8_t* partner)
{
    uint8_t roomOf[REMIX_MAX_ROOMS];   /* compact zone room -> room id */
    uint8_t stubs[REMIX_MAX_ROOMS][REMIX_MAX_DOORS];
    uint8_t nStub[REMIX_MAX_ROOMS];
    uint8_t order[REMIX_MAX_ROOMS];
    uint8_t open[REMIX_MAX_DOORS];
    int     nRooms = 0, nOpen = 0, nOrder, i, k;

    memset(nStub, 0, sizeof(nStub));

    for (i = 0; i < nz; i++)
    {
        int door = zoneDoors[i];
        for (k = 0; k < nRooms; k++)
            if (roomOf[k] == d[door].room)
                break;
        if (k == nRooms)
        {
            if (nRooms >= REMIX_MAX_ROOMS)
                return 0;
            roomOf[nRooms++] = d[door].room;
        }
        stubs[k][nStub[k]++] = (uint8_t)door;
    }

    if (nRooms < 2)
        return 0;

    for (k = 0; k < nRooms; k++)
    {
        shuffle_u8(r, stubs[k], nStub[k]);
        order[k] = (uint8_t)k;
    }
    shuffle_u8(r, order, nRooms);

    /* Root: a room with several doors, unless every room has one (then n == 2). */
    for (i = 0; i < nRooms; i++)
        if (nStub[order[i]] >= 2)
            break;
    if (i == nRooms)
        i = 0;
    {
        uint8_t t = order[0]; order[0] = order[i]; order[i] = t;
    }
    for (k = 0; k < nStub[order[0]]; k++)
        open[nOpen++] = stubs[order[0]][k];

    nOrder = nRooms - 1;
    {
        uint8_t* rest = &order[1];
        while (nOrder > 0)
        {
            int pick = rng_below(r, nOrder);
            int room, s, o;

            if (nOpen == 0)
                return 0;
            if (nOpen == 1 && nStub[rest[pick]] < 2)
            {
                for (i = 0; i < nOrder; i++)
                    if (nStub[rest[i]] >= 2)
                    {
                        pick = i;
                        break;
                    }
            }

            room = rest[pick];
            rest[pick] = rest[--nOrder];

            o = rng_below(r, nOpen);
            s = open[o];
            open[o] = open[--nOpen];
            link(partner, s, stubs[room][0]);
            for (k = 1; k < nStub[room]; k++)
                open[nOpen++] = stubs[room][k];
        }
    }

    if (nOpen & 1)
        return 0;
    return pair_rest(r, d, open, nOpen, partner);
}

int RemixLayout_Doors(const s_RemixMap* m, uint32_t seed, int mapIdx, uint8_t* partner)
{
    s_Rng   r;
    uint8_t zoneDoors[REMIX_MAX_DOORS];
    int     zone, i, changed = 0;

    for (i = 0; i < m->doorCount; i++)
        partner[i] = m->doors[i].twin;

    if (m->doorCount > REMIX_MAX_DOORS)
        return 0;

    rng_seed(&r, seed, mapIdx, 0xD0025u);

    for (zone = 0; zone < m->zoneCount; zone++)
    {
        int nz = 0, attempt;

        for (i = 0; i < m->doorCount; i++)
            if (m->doors[i].zone == zone)
                zoneDoors[nz++] = (uint8_t)i;

        /* One pair has only one possible wiring. */
        if (nz <= 2)
            continue;

        for (attempt = 0; attempt < 64; attempt++)
        {
            if (build_zone(&r, m->doors, zoneDoors, nz, partner))
                break;
            for (i = 0; i < nz; i++)
                partner[zoneDoors[i]] = m->doors[zoneDoors[i]].twin;
        }

        if (attempt < 64)
        {
            for (i = 0; i < nz; i++)
                if (partner[zoneDoors[i]] != m->doors[zoneDoors[i]].twin)
                {
                    changed++;
                    break;
                }
        }
    }
    return changed;
}

int RemixLayout_DoorGroup(const s_RemixDoorGroup* g, const s_RemixMap* maps, uint32_t seed, int groupIdx,
                          s_RemixDoorRef* refs, uint8_t* partner)
{
    static s_RemixDoor d[REMIX_MAX_DOORS];
    uint8_t keyMap[REMIX_MAX_ROOMS * 2], keyRoom[REMIX_MAX_ROOMS * 2], node[REMIX_MAX_ROOMS * 2];
    uint8_t all[REMIX_MAX_DOORS], setNode[256];
    int     n = 0, nKeys = 0, i, j, k, attempt;
    s_Rng   r;

    /* The group's doors, in member order. */
    for (i = 0; i < g->memberCount; i++)
    {
        const s_RemixMap* m = &maps[g->members[i].map];
        for (j = 0; j < m->doorCount; j++)
        {
            if (m->doors[j].zone != g->members[i].zone)
                continue;
            if (n >= REMIX_MAX_DOORS)
                return 0;
            refs[n].map  = g->members[i].map;
            refs[n].door = (uint8_t)j;
            d[n]         = m->doors[j];
            n++;
        }
    }
    if (n < 3)
        return 0;

    /* Rooms: one node per (map, room), merge sets joined into one node (sets that
     * share a room are one too). */
    for (i = 0; i < n; i++)
    {
        for (k = 0; k < nKeys && !(keyMap[k] == refs[i].map && keyRoom[k] == m_room(maps, refs, i)); k++)
            ;
        if (k == nKeys)
        {
            if (nKeys >= REMIX_MAX_ROOMS * 2)
                return 0;
            keyMap[k] = refs[i].map;
            keyRoom[k] = m_room(maps, refs, i);
            node[k] = (uint8_t)k;
            nKeys++;
        }
    }
    memset(setNode, 255, sizeof(setNode));
    for (i = 0; i < g->mergeCount; i++)
    {
        const s_RemixGroupMerge* mg = &g->merges[i];
        int a = -1;
        for (k = 0; k < nKeys; k++)
            if (keyMap[k] == mg->map && keyRoom[k] == mg->room)
                a = k;
        if (a < 0)
            continue;
        if (setNode[mg->set] == 255)
            setNode[mg->set] = node[a];
        else
        {
            /* join a's node and the set's node everywhere */
            uint8_t from = node[a], to = setNode[mg->set];
            for (k = 0; k < nKeys; k++)
                if (node[k] == from)
                    node[k] = to;
            for (k = 0; k < 256; k++)
                if (setNode[k] == from)
                    setNode[k] = to;
        }
    }
    /* compact node ids */
    {
        uint8_t remap[REMIX_MAX_ROOMS * 2], nn = 0;
        memset(remap, 255, sizeof(remap));
        for (k = 0; k < nKeys; k++)
        {
            if (remap[node[k]] == 255)
            {
                if (nn >= REMIX_MAX_ROOMS)
                    return 0;
                remap[node[k]] = nn++;
            }
        }
        for (i = 0; i < n; i++)
        {
            for (k = 0; k < nKeys && !(keyMap[k] == refs[i].map && keyRoom[k] == m_room(maps, refs, i)); k++)
                ;
            d[i].room = remap[node[k]];
        }
    }
    /* twins into group indices */
    for (i = 0; i < n; i++)
    {
        int t = maps[refs[i].map].doors[refs[i].door].twin;
        for (j = 0; j < n && !(refs[j].map == refs[i].map && refs[j].door == t); j++)
            ;
        if (j == n)
            return 0;
        d[i].twin = (uint8_t)j;
        all[i] = (uint8_t)i;
    }

    rng_seed(&r, seed, 0x400 + groupIdx, 0x6C00Fu);
    for (attempt = 0; attempt < 64; attempt++)
    {
        if (build_zone(&r, d, all, n, partner))
            break;
        for (i = 0; i < n; i++)
            partner[i] = d[i].twin;
    }
    if (attempt == 64)
        for (i = 0; i < n; i++)
            partner[i] = d[i].twin;
    return n;
}

void RemixLayout_Pickups(const s_RemixMap* m, uint32_t seed, int mapIdx, uint8_t* spot,
                         const uint8_t* exclude)
{
    s_Rng   r;
    uint8_t order[REMIX_MAX_SPOTS];
    uint8_t used[REMIX_MAX_SPOTS];
    uint8_t roomUsed[256];
    int     n = m->spotCount, i, k, pass;

    if (n > REMIX_MAX_SPOTS)
        n = REMIX_MAX_SPOTS;

    rng_seed(&r, seed, mapIdx, 0x17E35u);
    for (i = 0; i < n; i++)
        order[i] = (uint8_t)i;
    shuffle_u8(&r, order, n);

    memset(used, 0, sizeof(used));
    memset(roomUsed, 0, sizeof(roomUsed));
    for (i = 0; exclude && i < n; i++)
        used[i] = exclude[i] ? 1 : 0;
    for (k = 0; k < m->pickupCount; k++)
        spot[k] = 255;

    k = 0;
    for (pass = 0; pass < 2 && k < m->pickupCount; pass++)
    {
        for (i = 0; i < n && k < m->pickupCount; i++)
        {
            int s = order[i];
            if (used[s] || (pass == 0 && roomUsed[m->spots[s].room]))
                continue;
            used[s] = 1;
            roomUsed[m->spots[s].room] = 1;
            spot[k++] = (uint8_t)s;
        }
    }
}

static int pick_slot(s_Rng* r, const uint8_t* ok, int n)
{
    int count = 0, i, nth;
    for (i = 0; i < n; i++)
        count += ok[i] != 0;
    if (count == 0)
        return -1;
    nth = rng_below(r, count);
    for (i = 0; i < n; i++)
        if (ok[i] && nth-- == 0)
            return i;
    return -1;
}

const uint8_t* g_RemixLayoutBlocked;

void RemixLayout_Keys(const s_RemixMap* m, uint32_t seed, int mapIdx,
                      uint8_t* wide, uint8_t* cell, uint8_t* reserved)
{
    s_Rng   r;
    uint8_t order[REMIX_MAX_KEYS];
    uint8_t taken[REMIX_MAX_KEYSLOTS];
    uint8_t ok[REMIX_MAX_KEYSLOTS];
    int     nk = m->keyCount, ns = m->keySlotCount, i, j, k;

    if (nk > REMIX_MAX_KEYS)
        nk = REMIX_MAX_KEYS;
    if (ns > REMIX_MAX_KEYSLOTS)
        ns = REMIX_MAX_KEYSLOTS;

    memset(reserved, 0, (size_t)ns);
    for (k = 0; k < nk; k++)
        wide[k] = cell[k] = m->keys[k].slot;

    rng_seed(&r, seed, mapIdx, 0x4E75u);
    for (k = 0; k < nk; k++)
        order[k] = (uint8_t)k;
    shuffle_u8(&r, order, nk);

    /* Fallback first: a distinct slot in the key's own zone and cell. A swap
     * key's fallback is its own spot. */
    memset(taken, 0, sizeof(taken));
    for (j = 0; g_RemixLayoutBlocked != NULL && j < ns; j++)
        taken[j] = g_RemixLayoutBlocked[j];
    for (k = 0; k < nk; k++)
        if ((m->keys[k].swap || m->keys[k].pinned) && m->keys[k].slot < ns)
            taken[m->keys[k].slot] = 1;
    for (i = 0; i < nk; i++)
    {
        const s_RemixSlot* home;
        int pick;
        k = order[i];
        if (m->keys[k].slot >= ns || m->keys[k].swap || m->keys[k].pinned)
            continue;
        home = &m->keySlots[m->keys[k].slot];
        for (j = 0; j < ns; j++)
            ok[j] = !taken[j] && (m->keySlots[j].zone == home->zone || m->keys[k].anywhere) &&
                    m->keySlots[j].cellX == home->cellX && m->keySlots[j].cellZ == home->cellZ;
        pick = pick_slot(&r, ok, ns);
        if (pick < 0)
            pick = m->keys[k].slot; /* shares its vanilla spot with another key, as in vanilla */
        cell[k] = (uint8_t)pick;
        taken[pick] = 1;
    }

    /* Street keys (`swap`): each goes to a distinct spot among the street keys'
     * own spots (its own included: it may stay) and the pickups they may trade
     * with (swapOk).
     * A key on another street key's spot is fine, since that key has left it;
     * a key on a pickup's spot sends that pickup to a street key's spot that
     * nobody took (RemixLayout_KeyDisplaced). There are always exactly as many
     * of those as displaced pickups. */
    {
        uint8_t sk[REMIX_MAX_KEYS], tg[REMIX_MAX_KEYSLOTS], pickT[REMIX_MAX_KEYS];
        int     nsk = 0, nt = 0, tries, a2;

        for (i = 0; i < nk; i++)
        {
            k = order[i];
            if (m->keys[k].swap && !m->keys[k].pinned && m->keys[k].slot < ns)
                sk[nsk++] = (uint8_t)k;
        }
        for (j = 0; j < ns; j++)
        {
            int own = 0;
            for (a2 = 0; a2 < nsk; a2++)
                own |= m->keys[sk[a2]].slot == j;
            if (own || (m->keySlots[j].swapOk && m->keySlots[j].common != 255 && !taken[j]))
                tg[nt++] = (uint8_t)j;
        }
        for (tries = 0; nsk > 0 && tries < 64; tries++)
        {
            int good = 1;
            shuffle_u8(&r, tg, nt);
            for (a2 = 0; a2 < nsk && good; a2++)
            {
                int t;
                pickT[a2] = 255;
                for (t = 0; t < nt && pickT[a2] == 255; t++)
                {
                    int used = 0, b3;
                    for (b3 = 0; b3 < a2; b3++)
                        used |= pickT[b3] == tg[t];
                    if (!used)
                        pickT[a2] = tg[t];
                }
                good = pickT[a2] != 255;
            }
            if (good)
            {
                for (a2 = 0; a2 < nsk; a2++)
                    wide[sk[a2]] = pickT[a2];
                break;
            }
        }
        for (a2 = 0; a2 < nsk; a2++)
            taken[wide[sk[a2]]] = 1;
    }

    /* Wide: anywhere in the zone (or a slot proven reachable while it lies home,
     * `reach`), never on another key's fallback or wide slot. */
    for (i = 0; i < nk; i++)
    {
        const s_RemixSlot* home;
        int pick;
        k = order[i];
        if (m->keys[k].slot >= ns || m->keys[k].pinned || m->keys[k].swap)
            continue;
        home = &m->keySlots[m->keys[k].slot];
        for (j = 0; j < ns; j++)
            ok[j] = (!taken[j] || j == cell[k]) &&
                    (m->keySlots[j].zone == home->zone || m->keys[k].anywhere ||
                     (j < 64 && ((m->keys[k].reach >> j) & 1))) &&
                    !(m->keys[k].anywhere && m->keySlots[j].swapOk && j != m->keys[k].slot);
        pick = pick_slot(&r, ok, ns);
        wide[k] = (uint8_t)(pick < 0 ? cell[k] : pick);
        taken[wide[k]] = 1;
    }

    /* Keys with nowhere free to go but sharing a zone (or all optional on one
     * map) trade spots among themselves: a random rotation of their own spots,
     * so a room whose only item spots are its keys' still gets shuffled. Each
     * spot is left by its owner, so no two keys ever share a wide spot; only a
     * fallback home can meet the key that took that home. */
    {
        uint8_t done[REMIX_MAX_KEYS] = { 0 };
        int     a;

        for (a = 0; a < nk; a++)
        {
            uint8_t grp[REMIX_MAX_KEYS];
            int     n = 0, b2;
            const s_RemixSlot* ha;

            if (done[a] || m->keys[a].slot >= ns || m->keys[a].swap || m->keys[a].pinned ||
                wide[a] != m->keys[a].slot)
                continue;
            ha = &m->keySlots[m->keys[a].slot];
            for (b2 = a; b2 < nk; b2++)
            {
                const s_RemixSlot* hb;
                if (done[b2] || m->keys[b2].slot >= ns || m->keys[b2].swap || m->keys[b2].pinned ||
                    wide[b2] != m->keys[b2].slot)
                    continue;
                hb = &m->keySlots[m->keys[b2].slot];
                if ((m->keys[a].anywhere && m->keys[b2].anywhere) ||
                    (!m->keys[a].anywhere && !m->keys[b2].anywhere && hb->zone == ha->zone && ha->zone != REMIX_NO_ZONE))
                    grp[n++] = (uint8_t)b2;
            }
            for (b2 = 0; b2 < n; b2++)
                done[grp[b2]] = 1;
            /* Two keys on one vanilla spot gain nothing from trading. */
            for (b2 = 1; b2 < n; b2++)
                if (m->keys[grp[b2]].slot == m->keys[grp[0]].slot)
                    n = 0;
            if (n < 2)
                continue;
            shuffle_u8(&r, grp, n);
            for (b2 = 0; b2 < n; b2++)
                wide[grp[b2]] = m->keys[grp[(b2 + 1) % n]].slot;
        }
    }

    /* The vanilla spot too: a key waits there until classified, and returns
     * there as its last fallback. */
    for (k = 0; k < nk; k++)
    {
        reserved[wide[k]] = 1;
        reserved[cell[k]] = 1;
        if (m->keys[k].slot < ns)
            reserved[m->keys[k].slot] = 1;
    }
    for (j = 0; g_RemixLayoutBlocked != NULL && j < ns; j++)
        reserved[j] |= g_RemixLayoutBlocked[j];
}

void RemixLayout_KeyDisplaced(const s_RemixMap* m, uint32_t seed, int mapIdx,
                              const uint8_t* reserved, uint8_t* commonSlot)
{
    s_Rng   r;
    uint8_t taken[REMIX_MAX_KEYSLOTS];
    uint8_t ok[REMIX_MAX_KEYSLOTS];
    int     ns = m->keySlotCount, s, j;

    if (ns > REMIX_MAX_KEYSLOTS)
        ns = REMIX_MAX_KEYSLOTS;
    for (j = 0; j < m->pickupCount; j++)
        commonSlot[j] = 255;

    rng_seed(&r, seed, mapIdx, 0xD15Bu);
    memcpy(taken, reserved, (size_t)ns);
    {
        uint8_t wide[REMIX_MAX_KEYS], cell[REMIX_MAX_KEYS], res[REMIX_MAX_KEYSLOTS];
        int     k, nk = m->keyCount < REMIX_MAX_KEYS ? m->keyCount : REMIX_MAX_KEYS;

        /* A swap is a trade: a pickup whose spot a street key took goes to a
         * street key's own spot that no street key took. */
        uint8_t vacant[REMIX_MAX_KEYS];
        int     nv = 0, v = 0, k2;

        RemixLayout_Keys(m, seed, mapIdx, wide, cell, res);
        for (k = 0; k < nk; k++)
        {
            int took = 0;
            if (!m->keys[k].swap || m->keys[k].pinned || m->keys[k].slot >= ns)
                continue;
            for (k2 = 0; k2 < nk; k2++)
                took |= m->keys[k2].swap && wide[k2] == m->keys[k].slot;
            if (!took)
                vacant[nv++] = m->keys[k].slot;
        }
        for (k = 0; k < nk; k++)
        {
            const s_RemixSlot* sl = &m->keySlots[wide[k]];
            int onKeyHome = 0;
            /* On another street key's spot it shares it with whatever lay
             * there beside that key in vanilla (the Key of Lion's corner has a
             * pickup of its own). */
            for (k2 = 0; k2 < nk; k2++)
                onKeyHome |= m->keys[k2].swap && m->keys[k2].slot == wide[k];
            if (m->keys[k].swap && !onKeyHome && sl->common < m->pickupCount && v < nv)
                commonSlot[sl->common] = vacant[v++];
        }
    }
    for (s = 0; s < ns; s++)
    {
        const s_RemixSlot* sl = &m->keySlots[s];
        int pick;
        if (!reserved[s] || sl->common == 255 || sl->common >= m->pickupCount || commonSlot[sl->common] != 255)
            continue;
        /* A slot still holding its own common pickup is only reserved because a
         * key MIGHT land there; moving the pickup off keeps the two apart. */
        for (j = 0; j < ns; j++)
            ok[j] = !taken[j] && m->keySlots[j].zone == sl->zone;
        pick = pick_slot(&r, ok, ns);
        if (pick < 0)
            continue;
        commonSlot[sl->common] = (uint8_t)pick;
        taken[pick] = 1;
    }
}
