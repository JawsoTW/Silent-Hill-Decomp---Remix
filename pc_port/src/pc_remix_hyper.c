/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The inventory's two Hyper Blaster questions for the Remix add-on
 * (item_screens_1.c, item_screens_2.c). Kept out of pc_remix_host.c and asked
 * of the add-on's own table: an older pc_remix_host.c left in a source tree then
 * cannot leave these unresolved, and a DLL for another contract is ignored
 * here as it is there. Without the add-on each answers as the base game. */

#include "pc_remix.h"
#include "pc_remix_addon.h"
#include "dll_loader.h"

#include <stddef.h>

#ifdef _WIN32
#define REMIX_DLL "./SilentHillPC_RandomizerAddon.dll"
#elif defined(__APPLE__)
#define REMIX_DLL "./SilentHillPC_RandomizerAddon.dylib"
#else
#define REMIX_DLL "./SilentHillPC_RandomizerAddon.so"
#endif

static const s_RemixAddon* hyper_table(void)
{
    static int                 tried;
    static const s_RemixAddon* table;
    DllHandle                  dll;
    RemixAddon_GetFn           get;

    if (tried)
        return table;
    tried = 1;
    /* Already loaded by Pc_Remix_Init: this only takes another reference. */
    dll = DllLoader_Open(REMIX_DLL);
    if (dll == NULL)
        return NULL;
    get   = (RemixAddon_GetFn)DllLoader_GetSymbol(dll, REMIX_ADDON_GETTER);
    table = get ? get() : NULL;
    if (table != NULL && table->api != REMIX_ADDON_API)
        table = NULL;
    return table;
}

int Pc_RemixHyper_Grant(void)
{
    const s_RemixAddon* t = hyper_table();
    return t ? t->HyperBlasterGrant() : 1;
}

int Pc_RemixHyper_Works(void)
{
    const s_RemixAddon* t = hyper_table();
    return t ? t->HyperBlasterWorks() : 0;
}
