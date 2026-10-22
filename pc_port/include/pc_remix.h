/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_REMIX_H
#define PC_REMIX_H

/* The randomizer, called Remix in the code (its first name; config keys
 * remix_*, in randomizer.cfg). Interiors get a new, fully connected room layout
 * and their pickups move to new spots; progression doors are untouched. It never
 * writes map-DLL data: it installs its own copy of the map header with
 * rewritten event / mapPoint tables. Every entry point is a no-op when it is off.
 *
 * See pc_port/docs/Randomizer_Mode.md. */

#include "game.h"
#include "bodyprog/gfx/world_object.h"

/* Boot: loads the add-on, which reads randomizer.cfg. */
void Pc_Remix_Init(void);

/* Remix weapons: what a pickup really gives (Event_ItemTake), what an item
 * add really adds (the cafe's handgun, scripted pickups), and the starting
 * loadout of a game begun past the cafe. No-ops unless remix_weapons is on. */
void Pc_Remix_RemapItemTake(s32* itemId, s32* itemCount, s32 eventFlagIdx, s32* mapMsgIdx);
/* Remix items: the health or ammo type of the common pickup with this flag. */
u32  Pc_Remix_CommonType(u32 pickupType, s32 flag);
void Pc_Remix_RemapAddItem(s32* itemId, s32* itemCount);
void Pc_Remix_StartLoadout(u8* gunId, u8* gunCount, u8* ammoId, u8* ammoCount);

/* The inventory opening: 0 keeps the game from handing over the unlocked
 * Hyper Blaster, because Remix has it lying in the world or as the starting
 * gun. 1 (the game's own way) without the add-on or with Remix off.
 * The engine asks Pc_RemixHyper_Grant (pc_remix_hyper.c); the add-on's own is
 * Pc_Remix_HyperBlasterGrant, in its table. */
int  Pc_RemixHyper_Grant(void);
int  Pc_Remix_HyperBlasterGrant(void);
/* 1: the Hyper Blaster fires as an unlocked one, though the save has no UFO
 * ending (remix_unlockables_always put it in the world). 0 without the add-on.
 * Asked as Pc_RemixHyper_Works; the add-on's own is Pc_Remix_HyperBlasterWorks. */
int  Pc_RemixHyper_Works(void);
int  Pc_Remix_HyperBlasterWorks(void);

/* End of GameBoot_MapLoad, after the language patch. */
void Pc_Remix_OnMapLoad(s32 mapIdx);

/* True when the current map is running a remixed layout. */
int  Pc_Remix_MapActive(void);

/* True when any randomizer toggle is on. Every
 * engine hook checks this (or its own toggle) first, so with remix off the
 * game runs exactly the base code path. */
int  Pc_Remix_AnyOn(void);

/* GameBoot_MapLoad with remix off: release anything a remixed map left. */
void Pc_Remix_Off(void);

/* True when this map's monster spawns are being rerolled (remix enemies). */
int  Pc_Remix_EnemiesOn(void);

/* Remix keys: hide a moved key's original draw (world_draw.c). */
int  Pc_Remix_ObjectAddFilter(const s_WorldObjectModel* model, const VECTOR3* pos);

/* Gfx_WorldObjectsDraw: moved hospital plates, drawn with their inventory models. */
void Pc_Remix_WorldDraw(void);

/* Remix enemies: this spawn row's monster type (npc_main.c). */
void Pc_Remix_EnemyReroll(s32 row, s8* charaId, u8* stateStep);

/* Remix enemies' own sounds (pc_remix_sfx.c). */
void Pc_RemixSfx_OnMapLoad(const s8* types, int count, int ambientIdx);
void Pc_RemixSfx_Off(void);
int  Pc_RemixSfx_On(void);
void Pc_RemixSfx_Emitter(const VECTOR3* pos);
void Pc_RemixSfx_EmitterClear(void);
void Pc_RemixSfx_Updating(const void* npc);
u16  Pc_RemixSfx_VabPro(u16 sfxId, u16 vabPro);
u16  Pc_RemixSfx_StopVabPro(u16 sfxId, u16 vabPro);

#endif /* PC_REMIX_H */
