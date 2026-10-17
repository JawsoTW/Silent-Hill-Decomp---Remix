/* remix_layout_test.c -- Remix mode layout invariants over the REAL generated
 * tables (pc_remix_data.h), across many seeds.
 *
 *   cc -I pc_port/addons/remix/include pc_port/tests/remix_layout_test.c pc_port/addons/remix/src/pc_remix_layout.c
 *
 * For every remixable map and seed:
 *   1. pairing is symmetric and total (every door has exactly one partner)
 *   2. no door leads back into its own room
 *   3. partners never cross a zone (fixed-door progression structure is vanilla)
 *   4. every zone stays connected (no room can be cut off)
 *   5. deterministic: same (seed, map) -> same layout
 *   6. pickups land on distinct spots, spread over rooms when there are enough
 * and across all seeds, a zone with alternatives is actually remixed.
 *
 * With an argument, prints one seed's layout for map1_s01 (used by the docs). */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_remix_data.h"

static int find(int* p, int x)
{
    while (p[x] != x)
        x = p[x] = p[p[x]];
    return x;
}

static void check_map(int mapIdx, const s_RemixMap* m, uint32_t seed, int* changedOut)
{
    uint8_t partner[REMIX_MAX_DOORS], again[REMIX_MAX_DOORS];
    int     parent[REMIX_MAX_ROOMS];
    int     i, z;

    *changedOut += RemixLayout_Doors(m, seed, mapIdx, partner);
    RemixLayout_Doors(m, seed, mapIdx, again);
    assert(memcmp(partner, again, m->doorCount) == 0);

    for (i = 0; i < REMIX_MAX_ROOMS; i++)
        parent[i] = i;

    for (i = 0; i < m->doorCount; i++)
    {
        int j = partner[i];
        assert(j < m->doorCount);
        assert(j != i);
        assert(partner[j] == i);
        assert(m->doors[i].room != m->doors[j].room);
        assert(m->doors[i].zone == m->doors[j].zone);
        parent[find(parent, m->doors[i].room)] = find(parent, m->doors[j].room);
    }

    for (z = 0; z < m->zoneCount; z++)
    {
        int root = -1;
        for (i = 0; i < m->doorCount; i++)
        {
            if (m->doors[i].zone != z)
                continue;
            if (root < 0)
                root = find(parent, m->doors[i].room);
            assert(find(parent, m->doors[i].room) == root);
        }
    }

    if (m->pickupCount)
    {
        uint8_t spot[REMIX_MAX_PICKUPS];
        int     rooms = 0, distinctRooms = 0;
        uint8_t seenRoom[256] = { 0 }, seenSpot[REMIX_MAX_SPOTS] = { 0 };

        RemixLayout_Pickups(m, seed, mapIdx, spot, NULL);
        for (i = 0; i < m->spotCount; i++)
            if (!seenRoom[m->spots[i].room]++)
                rooms++;
        memset(seenRoom, 0, sizeof(seenRoom));
        for (i = 0; i < m->pickupCount; i++)
        {
            assert(spot[i] < m->spotCount);
            assert(!seenSpot[spot[i]]++);
            if (!seenRoom[m->spots[spot[i]].room]++)
                distinctRooms++;
        }
        assert(distinctRooms >= (m->pickupCount < rooms ? m->pickupCount : rooms));
    }

    if (m->keyCount)
    {
        uint8_t wide[REMIX_MAX_KEYS], cell[REMIX_MAX_KEYS], reserved[REMIX_MAX_KEYSLOTS];
        uint8_t displaced[REMIX_MAX_PICKUPS], used[REMIX_MAX_KEYSLOTS] = { 0 };
        uint8_t exclude[REMIX_MAX_SPOTS] = { 0 }, spot[REMIX_MAX_PICKUPS];
        int k, j, a, b;

        RemixLayout_Keys(m, seed, mapIdx, wide, cell, reserved);
        for (k = 0; k < m->keyCount; k++)
        {
            const s_RemixSlot* home = &m->keySlots[m->keys[k].slot];
            assert(wide[k] < m->keySlotCount && cell[k] < m->keySlotCount);
            /* Pinned (a weapon that only ever travels between areas): stays. */
            if (m->keys[k].pinned)
            {
                assert(wide[k] == m->keys[k].slot && cell[k] == m->keys[k].slot);
                continue;
            }
            /* Reachability: never leaves the key's zone. An optional bonus item
             * may go to any item spot; nothing else ever uses a zone-less one. */
            if (m->keys[k].swap)
            {
                /* its own spot, or any spot reachable before the door it opens */
                assert(cell[k] == m->keys[k].slot);
                {
                    int q, keyHome = 0;
                    for (q = 0; q < m->keyCount; q++)
                        keyHome |= m->keys[q].swap && m->keys[q].slot == wide[k];
                    assert(keyHome || m->keySlots[wide[k]].swapOk);
                }
            }
            else if (!m->keys[k].anywhere)
            {
                /* its zone, or a slot proven reachable while it lies home */
                assert((m->keySlots[wide[k]].zone == home->zone && home->zone != REMIX_NO_ZONE) ||
                       (wide[k] < 64 && ((m->keys[k].reach >> wide[k]) & 1)));
                assert(m->keySlots[cell[k]].zone == home->zone);
            }
            /* Visibility fallback: same IPD cell as the original. */
            assert(m->keySlots[cell[k]].cellX == home->cellX && m->keySlots[cell[k]].cellZ == home->cellZ);
            assert(reserved[wide[k]] && reserved[cell[k]] && reserved[m->keys[k].slot]);
        }
        /* No two keys ever share a spot as placed: wide spots are distinct and
         * off every other key's fallback spot -- except where keys trade their
         * own spots among themselves (a rotation), where the spot taken is one
         * its owner has left. */
        for (a = 0; a < m->keyCount; a++)
            for (b = 0; b < a; b++)
            {
                int sharedHome = m->keys[a].slot == m->keys[b].slot;
                assert(wide[a] != wide[b] || (sharedHome && wide[a] == m->keys[a].slot));
                assert(cell[a] != cell[b] || (sharedHome && cell[a] == m->keys[a].slot));
                assert(wide[a] != cell[b] || wide[b] != cell[b] || (sharedHome && wide[a] == m->keys[a].slot));
                assert(wide[b] != cell[a] || wide[a] != cell[a] || (sharedHome && wide[b] == m->keys[b].slot));
            }
        /* A pinned key (bonus room still locked) stays home, and nothing else
         * lands on its spot. */
        {
            static s_RemixKey kc[REMIX_MAX_KEYS];
            s_RemixMap        mc = *m;
            uint8_t           w2[REMIX_MAX_KEYS], c2[REMIX_MAX_KEYS], r2[REMIX_MAX_KEYSLOTS];
            int               p;
            memcpy(kc, m->keys, sizeof(s_RemixKey) * m->keyCount);
            mc.keys = kc;
            for (p = 0; p < m->keyCount; p++)
            {
                memcpy(kc, m->keys, sizeof(s_RemixKey) * m->keyCount);
                kc[p].pinned = 1;
                RemixLayout_Keys(&mc, seed, mapIdx, w2, c2, r2);
                assert(w2[p] == kc[p].slot && c2[p] == kc[p].slot);
                for (k = 0; k < m->keyCount; k++)
                    if (k != p && kc[k].slot != kc[p].slot)
                        assert(w2[k] != kc[p].slot && c2[k] != kc[p].slot);
            }
        }
        /* Displaced common pickups: to a free, unreserved slot in the same zone. */
        RemixLayout_KeyDisplaced(m, seed, mapIdx, reserved, displaced);
        for (j = 0; j < m->pickupCount; j++)
        {
            int origSlot = -1;
            if (displaced[j] == 255)
                continue;
            for (i = 0; i < m->keySlotCount; i++)
                if (m->keySlots[i].common == j)
                    origSlot = i;
            assert(origSlot >= 0 && reserved[origSlot]);
            {
                int sk, partnerOf = -1;
                for (sk = 0; sk < m->keyCount; sk++)
                    if (m->keys[sk].swap && wide[sk] == origSlot && wide[sk] != m->keys[sk].slot)
                        partnerOf = sk;
                if (partnerOf >= 0)
                {
                    /* the trade: to a street key's own spot no street key took */
                    int home = 0, took = 0;
                    for (sk = 0; sk < m->keyCount; sk++)
                    {
                        home |= m->keys[sk].swap && m->keys[sk].slot == displaced[j];
                        took |= m->keys[sk].swap && wide[sk] == displaced[j];
                    }
                    if (home && !took)
                        continue;
                    /* no street key's spot left over: moved like any other */
                }
            }
            assert(!reserved[displaced[j]] && !used[displaced[j]]++);
            assert(m->keySlots[displaced[j]].zone == m->keySlots[origSlot].zone);
        }
        /* Items mode with keys: excluded spots are never handed out. */
        for (i = 0; i < m->spotCount; i++)
            exclude[i] = (uint8_t)(i & 1);
        RemixLayout_Pickups(m, seed, mapIdx, spot, exclude);
        for (j = 0; j < m->pickupCount; j++)
            assert(spot[j] == 255 || !exclude[spot[j]]);
    }
}

/* Rooms across floors: every group's rewiring is symmetric, keeps every door
 * inside its group, never sends a door into its own room or merged node, and
 * leaves the group connected (counting the stairs/always-open doors that join
 * a merge set). And the same in every map of the group: it depends on the seed
 * and the group only. */
static void check_groups(void)
{
    int gi, crossSeen = 0, total = 0;
    for (gi = 0; gi < REMIX_DOOR_GROUP_COUNT; gi++)
    {
        const s_RemixDoorGroup* g = &REMIX_DOOR_GROUPS[gi];
        uint32_t seed;
        for (seed = 0; seed < 2000; seed++)
        {
            s_RemixDoorRef refs[REMIX_MAX_DOORS], refs2[REMIX_MAX_DOORS];
            uint8_t partner[REMIX_MAX_DOORS], partner2[REMIX_MAX_DOORS];
            int n = RemixLayout_DoorGroup(g, REMIX_MAPS, seed, gi, refs, partner), i, j, k;
            int key[REMIX_MAX_DOORS], parent[REMIX_MAX_DOORS], comps = 0;
            assert(n > 2);
            assert(RemixLayout_DoorGroup(g, REMIX_MAPS, seed, gi, refs2, partner2) == n);
            assert(!memcmp(refs, refs2, sizeof(refs[0]) * n) && !memcmp(partner, partner2, n));
            /* a node per door's (map, room), joined by merge sets */
            for (i = 0; i < n; i++)
            {
                key[i] = refs[i].map * 256 + REMIX_MAPS[refs[i].map].doors[refs[i].door].room;
                parent[i] = i;
            }
#define FIND(x) ({ int _x = (x); while (parent[_x] != _x) _x = parent[_x]; _x; })
            for (i = 0; i < n; i++)
                for (j = 0; j < i; j++)
                    if (key[i] == key[j])
                        parent[FIND(i)] = FIND(j);
            for (k = 0; k < g->mergeCount; k++)
                for (j = 0; j < g->mergeCount; j++)
                    if (g->merges[k].set == g->merges[j].set)
                        for (i = 0; i < n; i++)
                            if (key[i] == g->merges[k].map * 256 + g->merges[k].room)
                            {
                                int q;
                                for (q = 0; q < n; q++)
                                    if (key[q] == g->merges[j].map * 256 + g->merges[j].room)
                                        parent[FIND(i)] = FIND(q);
                            }
            {
                int nodeParent[REMIX_MAX_DOORS];
                memcpy(nodeParent, parent, sizeof(parent));
                for (i = 0; i < n; i++)
                {
                    assert(partner[i] < n && partner[partner[i]] == i && partner[i] != i);
                    { int a2 = i, b2 = partner[i];
                      while (nodeParent[a2] != a2) a2 = nodeParent[a2];
                      while (nodeParent[b2] != b2) b2 = nodeParent[b2];
                      assert(a2 != b2); }                          /* never into its own node */
                    crossSeen += refs[i].map != refs[partner[i]].map;
                    total++;
                    parent[FIND(i)] = FIND(partner[i]);
                }
            }
            for (i = 0; i < n; i++)
                comps += FIND(i) == i;
            assert(comps == 1);                                      /* the group stays connected */
#undef FIND
        }
    }
    printf("rooms across floors: %d groups x 2000 seeds; %d of %d doors lead to another floor\n",
           REMIX_DOOR_GROUP_COUNT, crossSeen, total);
    assert(REMIX_DOOR_GROUP_COUNT == 0 || crossSeen > 0);
}

int main(int argc, char** argv)
{
    int mapIdx, maps = 0;
    uint32_t seed;

    if (argc > 1)
    {
        const s_RemixMap* m = &REMIX_MAPS[4];
        uint8_t partner[REMIX_MAX_DOORS], spot[REMIX_MAX_PICKUPS];
        int i;
        seed = (uint32_t)strtoul(argv[1], NULL, 10);
        RemixLayout_Doors(m, seed, 4, partner);
        RemixLayout_Pickups(m, seed, 4, spot, NULL);
        for (i = 0; i < m->doorCount; i++)
            printf("door %d %d\n", i, partner[i]);
        for (i = 0; i < m->pickupCount; i++)
            printf("pickup %d %d\n", i, spot[i]);
        return 0;
    }

    for (mapIdx = 0; mapIdx < REMIX_MAP_COUNT; mapIdx++)
    {
        const s_RemixMap* m = &REMIX_MAPS[mapIdx];
        int changed = 0, multiZone = 0, z, i;
        if (m->doorCount == 0 && m->pickupCount == 0 && m->keyCount == 0)
            continue;
        maps++;
        for (seed = 0; seed < 2000; seed++)
            check_map(mapIdx, m, seed, &changed);

        /* a zone with more than one pair has alternatives; tiny zones only have two
         * wirings (vanilla or crossed), so expect change in well over a third */
        for (z = 0; z < m->zoneCount; z++)
        {
            int n = 0;
            for (i = 0; i < m->doorCount; i++)
                n += m->doors[i].zone == z;
            multiZone += n > 2;
        }
        printf("map %2d: %2d doors, %d zones (%d remixable): zones changed in %d/%d seeds-zones\n",
               mapIdx, m->doorCount, m->zoneCount, multiZone, changed, 2000 * multiZone);
        fflush(stdout);
        assert(multiZone == 0 || changed > 2000 * multiZone / 3);
    }
    check_groups();
    printf("OK: %d maps x 2000 seeds\n", maps);
    return 0;
}
