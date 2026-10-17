/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Force-included into every add-on source (CMakeLists.txt). The exe defines
 * the Pc_Remix_* names as forwarders to this DLL (pc_remix_host.c); the
 * add-on's own implementations get distinct names so neither side can bind
 * to the other's copy (ELF interposition, a stray import). */
#ifndef REMIX_ADDON_NAMES_H
#define REMIX_ADDON_NAMES_H

#define Pc_Remix_Init            Rmx_Init
#define Pc_Remix_AnyOn           Rmx_AnyOn
#define Pc_Remix_OnMapLoad       Rmx_OnMapLoad
#define Pc_Remix_Off             Rmx_Off
#define Pc_Remix_MapActive       Rmx_MapActive
#define Pc_Remix_EnemiesOn       Rmx_EnemiesOn
#define Pc_Remix_ObjectAddFilter Rmx_ObjectAddFilter
#define Pc_Remix_WorldDraw       Rmx_WorldDraw
#define Pc_Remix_EnemyReroll     Rmx_EnemyReroll
#define Pc_Remix_CommonType      Rmx_CommonType
#define Pc_Remix_RemapItemTake   Rmx_RemapItemTake
#define Pc_Remix_RemapAddItem    Rmx_RemapAddItem
#define Pc_Remix_StartLoadout    Rmx_StartLoadout
#define Pc_RemixSfx_OnMapLoad    RmxSfx_OnMapLoad
#define Pc_RemixSfx_Off          RmxSfx_Off
#define Pc_RemixSfx_On           RmxSfx_On
#define Pc_RemixSfx_Emitter      RmxSfx_Emitter
#define Pc_RemixSfx_EmitterClear RmxSfx_EmitterClear
#define Pc_RemixSfx_Updating     RmxSfx_Updating
#define Pc_RemixSfx_VabPro       RmxSfx_VabPro
#define Pc_RemixSfx_StopVabPro   RmxSfx_StopVabPro

#endif /* REMIX_ADDON_NAMES_H */
