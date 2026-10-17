/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_OPTIONS_ADDON_H
#define PC_OPTIONS_ADDON_H

/* A page an add-on DLL (addons/, e.g. SilentHillPC_RemixAddon) puts into PC Options.
 * It shows right after the Controls page. The add-on owns the values and the
 * file they live in: the menu edits *field in place and hands each change to
 * save(), never to config.cfg. Labels go through the menu translation table
 * like every other row, so an unknown label shows as written (underscores as
 * spaces). */

enum {
    PCADDON_ONOFF = 0, /* field is an int, 0 or 1 */
    PCADDON_SEED  = 1  /* field is an unsigned long long seed; left/right rolls a new one
                        * (1..PCADDON_SEED_MAX) */
};

typedef struct {
    const char* name;
    const char* key;
    void*       field;
    int         kind;
} s_PcAddonOpt;

/* The page fits PCADDON_MAX_ROWS value rows above its Prev/Next/Back rows. */
#define PCADDON_MAX_ROWS 9

/* Eight digits: easy to read off and share. */
#define PCADDON_SEED_MAX 99999999ULL

typedef struct {
    const s_PcAddonOpt* rows;
    int                 count;
    void (*save)(const char* key, const char* value);
    /* After a row changed (left/right or reset), for the add-on's own notice. May be NULL. */
    void (*changed)(const char* key);
} s_PcAddonPage;

/* Copies the page description (rows must stay valid). NULL removes the page. */
void PcOpt_AddonPageSet(const s_PcAddonPage* page);

#endif /* PC_OPTIONS_ADDON_H */
