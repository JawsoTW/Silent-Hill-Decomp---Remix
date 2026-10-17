/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_REMIX_CFG_H
#define PC_REMIX_CFG_H

/* The add-on's settings, from remix.cfg next to config.cfg (pc_remix_cfg.c).
 * Keys and meanings: remix.cfg itself and docs/Remix_Mode.md. */

typedef struct {
    int  remix;                  /* remix: rooms */
    int  remixItems;             /* remix_items */
    int  remixKeys;              /* remix_keys */
    int  remixUnlockables;       /* remix_unlockables */
    char remixFixedItems[512];   /* remix_fixed_items: InvItemId names that stay put */
    int  remixStarterWeapons;    /* remix_starter_weapons */
    int  remixWeapons;           /* remix_weapons */
    int  remixEnemies;           /* remix_enemies */
    int  remixWeights[6];        /* remix_weight_*, by CommonPickupItemId */
    char remixEnemyPools[9][192]; /* remix_enemies_<zone> */
    int  remixSewerScratchers;   /* remix_enemies_sewer_scratchers */
    unsigned long long remixSeed; /* remix_seed, 0..REMIX_SEED_MAX */
    char remixMaps[256];         /* remix_maps */
} s_RemixCfg;

/* Eight digits; a bigger remix_seed is cut to it. */
#define REMIX_SEED_MAX 99999999ULL

extern s_RemixCfg g_RemixCfg;

/* Reads remix.cfg. Without one, takes the remix_* keys an older build left in
 * config.cfg and writes remix.cfg from them. */
void RemixCfg_Load(void);
/* The seed as the layout code takes it (32 bits). */
unsigned int RemixCfg_Seed32(void);
/* Rewrites one key's line in remix.cfg (appends it when absent). */
void RemixCfg_SaveKey(const char* key, const char* value);

#endif /* PC_REMIX_CFG_H */
