/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_REMIX_ADDON_H
#define PC_REMIX_ADDON_H

/* The contract between the exe (pc_remix_host.c) and the randomizer add-on DLL
 * (addons/remix, SilentHillPC_RandomizerAddon.dll / .so). The engine calls the
 * Pc_Remix_* entry points in pc_remix.h; the host forwards each one to this
 * table, or does the vanilla thing when the DLL is absent. A DLL built for a
 * different REMIX_ADDON_API is not used. */

#include "game.h"

#define REMIX_ADDON_API     4
#define REMIX_ADDON_GETTER  "RemixAddon_Get"

typedef struct {
    int api;
    /* Boot, after config.cfg. The DLL reads randomizer.cfg and
     * adds its PC Options page here. */
    void (*Init)(void);
    int  (*AnyOn)(void);
    void (*OnMapLoad)(s32 mapIdx);
    void (*Off)(void);
    int  (*MapActive)(void);
    int  (*EnemiesOn)(void);
    /* remix_enemies is set (player_control.c hop rows). */
    int  (*EnemiesWanted)(void);
    int  (*ObjectAddFilter)(const s_WorldObjectModel* model, const VECTOR3* pos);
    void (*WorldDraw)(void);
    void (*EnemyReroll)(s32 row, s8* charaId, u8* stateStep);
    u32  (*CommonType)(u32 pickupType, s32 flag);
    void (*RemapItemTake)(s32* itemId, s32* itemCount, s32 eventFlagIdx, s32* mapMsgIdx);
    void (*RemapAddItem)(s32* itemId, s32* itemCount);
    void (*StartLoadout)(u8* gunId, u8* gunCount, u8* ammoId, u8* ammoCount);
    int  (*SfxOn)(void);
    void (*SfxEmitter)(const VECTOR3* pos);
    void (*SfxEmitterClear)(void);
    void (*SfxUpdating)(const void* npc);
    u16  (*SfxVabPro)(u16 sfxId, u16 vabPro);
    u16  (*SfxStopVabPro)(u16 sfxId, u16 vabPro);
    int  (*HyperBlasterGrant)(void);
    int  (*HyperBlasterWorks)(void);
} s_RemixAddon;

typedef const s_RemixAddon* (*RemixAddon_GetFn)(void);

#endif /* PC_REMIX_ADDON_H */
