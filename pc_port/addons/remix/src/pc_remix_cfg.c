/* SPDX-License-Identifier: GPL-3.0-or-later */
/* remix.cfg: the add-on keeps its settings out of config.cfg, so removing the
 * DLL leaves the game's own config exactly as the base game writes it. Same
 * "key = value" format, so the Remix launcher shares the main one's parser. */

#include "pc_remix_cfg.h"
#include "remix_cfg_template.h"
#include "sh_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REMIX_CFG_PATH  "remix.cfg"
#define GAME_CFG_PATH   "config.cfg"
#define REMIX_CFG_LINE  1024
#define REMIX_CFG_MAX   (256 * 1024)

s_RemixCfg g_RemixCfg = {
    .remixWeights = { 27, 69, 4, 67, 14, 19 }, /* the vanilla game's own proportions, rounded */
};

static const char* const WEIGHT_NAMES[6] = { "first_aid", "health_drink", "ampoule",
                                             "handgun_ammo", "rifle_ammo", "shotgun_ammo" };
static const char* const ZONE_NAMES[9] = { "outside", "outside_otherworld", "school", "otherworld_school",
                                           "hospital", "buildings", "nowhere", "park", "sewers" };

static void copy_str(char* dst, size_t cap, const char* src)
{
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static int apply(const char* key, const char* value)
{
    int i;

    if (strcmp(key, "remix") == 0)                       g_RemixCfg.remix = atoi(value) != 0;
    else if (strcmp(key, "remix_items") == 0)            g_RemixCfg.remixItems = atoi(value) != 0;
    else if (strcmp(key, "remix_keys") == 0)             g_RemixCfg.remixKeys = atoi(value) != 0;
    else if (strcmp(key, "remix_starter_weapons") == 0)  g_RemixCfg.remixStarterWeapons = atoi(value) != 0;
    else if (strcmp(key, "remix_weapons") == 0)          g_RemixCfg.remixWeapons = atoi(value) != 0;
    else if (strcmp(key, "remix_unlockables") == 0)      g_RemixCfg.remixUnlockables = atoi(value) != 0;
    else if (strcmp(key, "remix_enemies") == 0)          g_RemixCfg.remixEnemies = atoi(value) != 0;
    else if (strcmp(key, "remix_enemies_sewer_scratchers") == 0)
        g_RemixCfg.remixSewerScratchers = atoi(value) != 0;
    else if (strcmp(key, "remix_seed") == 0) {
        unsigned long long v = strtoull(value, NULL, 10);
        g_RemixCfg.remixSeed = v > REMIX_SEED_MAX ? REMIX_SEED_MAX : v;
    }
    else if (strcmp(key, "remix_maps") == 0)
        copy_str(g_RemixCfg.remixMaps, sizeof(g_RemixCfg.remixMaps), value);
    else if (strcmp(key, "remix_fixed_items") == 0)
        copy_str(g_RemixCfg.remixFixedItems, sizeof(g_RemixCfg.remixFixedItems), value);
    else if (strncmp(key, "remix_weight_", 13) == 0) {
        int v = atoi(value);
        for (i = 0; i < 6; i++)
            if (strcmp(key + 13, WEIGHT_NAMES[i]) == 0)
                g_RemixCfg.remixWeights[i] = v < 0 ? 0 : (v > 100 ? 100 : v);
    } else if (strncmp(key, "remix_enemies_", 14) == 0) {
        for (i = 0; i < 9; i++)
            if (strcmp(key + 14, ZONE_NAMES[i]) == 0)
                copy_str(g_RemixCfg.remixEnemyPools[i], sizeof(g_RemixCfg.remixEnemyPools[i]), value);
    } else
        return 0;
    return 1;
}

static char* trim(char* p)
{
    char* e;
    while (*p == ' ' || *p == '\t')
        p++;
    e = p + strlen(p);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
    return p;
}

/* Splits "key = value" in place; 0 for comments, blanks and lines without '='. */
static int split(char* line, char** key, char** value)
{
    char* p = trim(line);
    char* eq;
    if (*p == '#' || *p == ';' || *p == '\0')
        return 0;
    eq = strchr(p, '=');
    if (eq == NULL)
        return 0;
    *eq    = '\0';
    *key   = trim(p);
    *value = trim(eq + 1);
    return 1;
}

/* Returns how many remix keys were read, or -1 when the file is missing. */
static int parse(const char* path)
{
    char  line[REMIX_CFG_LINE];
    char *key, *value;
    int   n = 0;
    FILE* f = fopen(path, "r");

    if (f == NULL)
        return -1;
    while (fgets(line, sizeof(line), f))
        if (split(line, &key, &value) && strncmp(key, "remix", 5) == 0)
            n += apply(key, value);
    fclose(f);
    return n;
}

/* The documented template, then every value in effect written over its
 * default, so a migrated config.cfg's settings carry over. */
static void write_all(void)
{
    char  v[32];
    int   i;
    FILE* f = fopen(REMIX_CFG_PATH, "wb");

    if (f == NULL) {
        SH_LOG("[REMIX] cannot write %s", REMIX_CFG_PATH);
        return;
    }
    fwrite(REMIX_CFG_TEMPLATE, 1, sizeof(REMIX_CFG_TEMPLATE) - 1, f);
    fclose(f);

#define SAVE_INT(k, x) do { snprintf(v, sizeof(v), "%d", (x)); RemixCfg_SaveKey((k), v); } while (0)
    SAVE_INT("remix", g_RemixCfg.remix);
    SAVE_INT("remix_items", g_RemixCfg.remixItems);
    SAVE_INT("remix_keys", g_RemixCfg.remixKeys);
    SAVE_INT("remix_starter_weapons", g_RemixCfg.remixStarterWeapons);
    SAVE_INT("remix_weapons", g_RemixCfg.remixWeapons);
    SAVE_INT("remix_unlockables", g_RemixCfg.remixUnlockables);
    SAVE_INT("remix_enemies", g_RemixCfg.remixEnemies);
    SAVE_INT("remix_enemies_sewer_scratchers", g_RemixCfg.remixSewerScratchers);
    for (i = 0; i < 6; i++) {
        char k[48];
        snprintf(k, sizeof(k), "remix_weight_%s", WEIGHT_NAMES[i]);
        SAVE_INT(k, g_RemixCfg.remixWeights[i]);
    }
#undef SAVE_INT
    for (i = 0; i < 9; i++) {
        char k[48];
        snprintf(k, sizeof(k), "remix_enemies_%s", ZONE_NAMES[i]);
        RemixCfg_SaveKey(k, g_RemixCfg.remixEnemyPools[i][0] ? g_RemixCfg.remixEnemyPools[i] : "default");
    }
    RemixCfg_SaveKey("remix_fixed_items", g_RemixCfg.remixFixedItems);
    snprintf(v, sizeof(v), "%llu", g_RemixCfg.remixSeed);
    RemixCfg_SaveKey("remix_seed", v);
    RemixCfg_SaveKey("remix_maps", g_RemixCfg.remixMaps);
}

/* Every seed up to REMIX_SEED_MAX fits in 32 bits and is used as is, so a seed
 * keeps the layout it always had. */
unsigned int RemixCfg_Seed32(void)
{
    unsigned long long s = g_RemixCfg.remixSeed;
    return (unsigned int)s ^ (unsigned int)(s >> 32) * 0x9E3779B1u;
}

void RemixCfg_Load(void)
{
    int n = parse(REMIX_CFG_PATH);

    if (n >= 0) {
        SH_LOG("[REMIX] %s: %d settings", REMIX_CFG_PATH, n);
        return;
    }
    n = parse(GAME_CFG_PATH);
    write_all();
    SH_LOG("[REMIX] %s not found -- created it (%d settings taken from %s)",
           REMIX_CFG_PATH, n > 0 ? n : 0, GAME_CFG_PATH);
}

void RemixCfg_SaveKey(const char* key, const char* value)
{
    char*  buf;
    char*  out;
    size_t len = 0, outLen = 0, klen = strlen(key);
    int    found = 0;
    FILE*  f = fopen(REMIX_CFG_PATH, "rb");

    buf = (char*)malloc(REMIX_CFG_MAX);
    out = (char*)malloc(REMIX_CFG_MAX + REMIX_CFG_LINE);
    if (buf == NULL || out == NULL) {
        free(buf);
        free(out);
        return;
    }
    if (f != NULL) {
        len = fread(buf, 1, REMIX_CFG_MAX - 1, f);
        fclose(f);
    }
    buf[len] = '\0';

    {
        char* line = buf;
        while (*line) {
            char*  nl   = strchr(line, '\n');
            size_t llen = nl ? (size_t)(nl - line + 1) : strlen(line);
            const char* p = line;
            while (*p == ' ' || *p == '\t')
                p++;
            if (!found && strncmp(p, key, klen) == 0 &&
                (p[klen] == ' ' || p[klen] == '\t' || p[klen] == '=')) {
                outLen += (size_t)snprintf(out + outLen, REMIX_CFG_LINE, "%s = %s\n", key, value);
                found = 1;
            } else {
                memcpy(out + outLen, line, llen);
                outLen += llen;
                if (nl == NULL) {
                    out[outLen++] = '\n';
                }
            }
            line += llen;
        }
    }
    if (!found)
        outLen += (size_t)snprintf(out + outLen, REMIX_CFG_LINE, "%s = %s\n", key, value);

    f = fopen(REMIX_CFG_PATH, "wb");
    if (f != NULL) {
        fwrite(out, 1, outLen, f);
        fclose(f);
    } else {
        SH_LOG("[REMIX] cannot write %s", REMIX_CFG_PATH);
    }
    free(buf);
    free(out);
}
