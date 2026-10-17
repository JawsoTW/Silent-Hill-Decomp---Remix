/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The add-on's face to the exe: the table pc_remix_host.c forwards the engine
 * hooks through, and the Remix page it puts into PC Settings. */

#include "pc_remix.h"
#include "pc_remix_addon.h"
#include "pc_remix_cfg.h"
#include "pc_options_addon.h"
#include "pc_config.h"
#include "sh_log.h"

#include <string.h>

static const s_PcAddonOpt ROWS[] = {
    { "Remix_Rooms",       "remix",                 &g_RemixCfg.remix,               PCADDON_ONOFF },
    { "Remix_Items",       "remix_items",           &g_RemixCfg.remixItems,          PCADDON_ONOFF },
    { "Remix_Keys",        "remix_keys",            &g_RemixCfg.remixKeys,           PCADDON_ONOFF },
    { "Remix_Enemies",     "remix_enemies",         &g_RemixCfg.remixEnemies,        PCADDON_ONOFF },
    { "Remix_Start_Gun",   "remix_starter_weapons", &g_RemixCfg.remixStarterWeapons, PCADDON_ONOFF },
    { "Remix_Weapons",     "remix_weapons",         &g_RemixCfg.remixWeapons,        PCADDON_ONOFF },
    { "Remix_Unlockables", "remix_unlockables",     &g_RemixCfg.remixUnlockables,    PCADDON_ONOFF },
    { "Seed_(8_digits)",   "remix_seed",            &g_RemixCfg.remixSeed,           PCADDON_SEED  },
};

static void page_changed(const char* key)
{
    if (strcmp(key, "remix_seed") == 0)
        SH_DBG_ECHO("Seeds run from 1 to %llu (8 digits).", REMIX_SEED_MAX);
    if (g_PcConfig.randomizer)
        SH_DBG_ECHO("Remix is ignored while the randomizer is on.");
    else if (strcmp(key, "remix_starter_weapons") == 0)
        SH_DBG_ECHO("The starting gun applies to a new game.");
    else
        SH_DBG_ECHO("Remix applies the next time an area loads.");
}

static void addon_init(void)
{
    s_PcAddonPage page;

    RemixCfg_Load();
    page.rows    = ROWS;
    page.count   = (int)(sizeof(ROWS) / sizeof(ROWS[0]));
    page.save    = RemixCfg_SaveKey;
    page.changed = page_changed;
    PcOpt_AddonPageSet(&page);
    Pc_Remix_Init();
}

static int enemies_wanted(void)
{
    return g_RemixCfg.remixEnemies;
}

static const s_RemixAddon s_table = {
    REMIX_ADDON_API,
    addon_init,
    Pc_Remix_AnyOn,
    Pc_Remix_OnMapLoad,
    Pc_Remix_Off,
    Pc_Remix_MapActive,
    Pc_Remix_EnemiesOn,
    enemies_wanted,
    Pc_Remix_ObjectAddFilter,
    Pc_Remix_WorldDraw,
    Pc_Remix_EnemyReroll,
    Pc_Remix_CommonType,
    Pc_Remix_RemapItemTake,
    Pc_Remix_RemapAddItem,
    Pc_Remix_StartLoadout,
    Pc_RemixSfx_On,
    Pc_RemixSfx_Emitter,
    Pc_RemixSfx_EmitterClear,
    Pc_RemixSfx_Updating,
    Pc_RemixSfx_VabPro,
    Pc_RemixSfx_StopVabPro,
};

#ifdef _WIN32
__declspec(dllexport)
#endif
const s_RemixAddon* RemixAddon_Get(void)
{
    return &s_table;
}
