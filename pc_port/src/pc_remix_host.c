/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Remix mode lives in an add-on DLL (addons/remix, docs/Remix_Mode.md). These
 * are the engine's entry points (pc_remix.h): each forwards to the add-on's
 * table, and without the add-on each does what the base game does, so the
 * engine call sites need no #ifdefs and the exe runs vanilla. */

#include "pc_remix.h"
#include "pc_remix_addon.h"
#include "dll_loader.h"
#include "sh_log.h"

#include <stddef.h>

#ifdef _WIN32
#define REMIX_DLL "./SilentHillPC_RemixAddon.dll"
#elif defined(__APPLE__)
#define REMIX_DLL "./SilentHillPC_RemixAddon.dylib"
#else
#define REMIX_DLL "./SilentHillPC_RemixAddon.so"
#endif

static const s_RemixAddon* s_rmx;

void Pc_Remix_Init(void)
{
    DllHandle        dll = DllLoader_Open(REMIX_DLL);
    RemixAddon_GetFn get;
    const s_RemixAddon* t;

    if (dll == NULL) {
        SH_LOG("[REMIX] add-on not installed (%s)", REMIX_DLL);
        return;
    }
    get = (RemixAddon_GetFn)DllLoader_GetSymbol(dll, REMIX_ADDON_GETTER);
    t   = get ? get() : NULL;
    if (t == NULL || t->api != REMIX_ADDON_API) {
        SH_LOG("[REMIX] %s is for a different game build (api %d, want %d) -- not used",
               REMIX_DLL, t ? t->api : -1, REMIX_ADDON_API);
        DllLoader_Close(dll);
        return;
    }
    s_rmx = t;
    SH_LOG("[REMIX] add-on loaded (%s)", REMIX_DLL);
    s_rmx->Init();
}

int Pc_Remix_AnyOn(void)                { return s_rmx ? s_rmx->AnyOn() : 0; }
void Pc_Remix_OnMapLoad(s32 mapIdx)     { if (s_rmx) s_rmx->OnMapLoad(mapIdx); }
void Pc_Remix_Off(void)                 { if (s_rmx) s_rmx->Off(); }
int Pc_Remix_MapActive(void)            { return s_rmx ? s_rmx->MapActive() : 0; }
int Pc_Remix_EnemiesOn(void)            { return s_rmx ? s_rmx->EnemiesOn() : 0; }
int Pc_Remix_EnemiesWanted(void)        { return s_rmx ? s_rmx->EnemiesWanted() : 0; }
void Pc_Remix_WorldDraw(void)           { if (s_rmx) s_rmx->WorldDraw(); }

int Pc_Remix_ObjectAddFilter(const s_WorldObjectModel* model, const VECTOR3* pos)
{
    return s_rmx ? s_rmx->ObjectAddFilter(model, pos) : 0;
}

void Pc_Remix_EnemyReroll(s32 row, s8* charaId, u8* stateStep)
{
    if (s_rmx) s_rmx->EnemyReroll(row, charaId, stateStep);
}

u32 Pc_Remix_CommonType(u32 pickupType, s32 flag)
{
    return s_rmx ? s_rmx->CommonType(pickupType, flag) : pickupType;
}

void Pc_Remix_RemapItemTake(s32* itemId, s32* itemCount, s32 eventFlagIdx, s32* mapMsgIdx)
{
    if (s_rmx) s_rmx->RemapItemTake(itemId, itemCount, eventFlagIdx, mapMsgIdx);
}

void Pc_Remix_RemapAddItem(s32* itemId, s32* itemCount)
{
    if (s_rmx) s_rmx->RemapAddItem(itemId, itemCount);
}

void Pc_Remix_StartLoadout(u8* gunId, u8* gunCount, u8* ammoId, u8* ammoCount)
{
    if (s_rmx) s_rmx->StartLoadout(gunId, gunCount, ammoId, ammoCount);
}

int Pc_RemixSfx_On(void)                { return s_rmx ? s_rmx->SfxOn() : 0; }
void Pc_RemixSfx_Emitter(const VECTOR3* pos) { if (s_rmx) s_rmx->SfxEmitter(pos); }
void Pc_RemixSfx_EmitterClear(void)     { if (s_rmx) s_rmx->SfxEmitterClear(); }
void Pc_RemixSfx_Updating(const void* npc) { if (s_rmx) s_rmx->SfxUpdating(npc); }
u16 Pc_RemixSfx_VabPro(u16 sfxId, u16 vabPro)     { return s_rmx ? s_rmx->SfxVabPro(sfxId, vabPro) : vabPro; }
u16 Pc_RemixSfx_StopVabPro(u16 sfxId, u16 vabPro) { return s_rmx ? s_rmx->SfxStopVabPro(sfxId, vabPro) : vabPro; }
