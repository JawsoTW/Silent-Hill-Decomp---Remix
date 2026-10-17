/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Remix enemies: each monster keeps its own sounds outside its home maps.
 *
 * A monster's sound effects are programs in "the" ambient VAB (libsd slot 2),
 * and every map loads its own ambient bank there. A monster's SFX ids (the
 * global g_Vab_InfoTable) name program numbers that only mean the right thing
 * in the banks of the maps that monster lives in; anywhere else the same
 * program is some other sound, or nothing.
 *
 * Where the borrowed sounds go depends on the sound backend. PsyCross's
 * legacy OpenAL SPU, and the software SPU with its extended RAM, keep 2 MiB of
 * sample memory while the game never leaves the PSX's 512 KiB: there each
 * home bank is loaded whole above 512 KiB at map load, where nothing the game
 * does can touch it. A software SPU without the extension wraps every address
 * to 512 KiB (writing there once overwrote BASE.VAB and changed Harry's
 * footsteps), so with it only the samples a foreign monster's own sound ids
 * use are packed into the unused tail of the ambient slot (from the end of the
 * map's own ambient bank up to the music bank's fixed start), under a small
 * VAB whose VAG size table is rewritten so each packed sample sits where libsd
 * looks for it; types that don't fit keep the map's own sounds. Which case
 * applies is probed by asking the SPU for a transfer address above 512 KiB.
 *
 * Tail packing waits for the first sound a foreign monster plays (the map's
 * own ambient bank must be in first, its size decides where the tail starts),
 * and is redone whenever the ambient bank changes. When a sound is played from a
 * monster's position (the Sfx_With* entry points, which every monster uses),
 * the emitter's type is latched; an ambient-bank sound from a foreign monster
 * then keys on from its borrowed set instead of slot 2. Ground-material
 * footsteps, Harry's sounds and everything else play exactly as before. */

#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "bodyprog/bodyprog.h"
#include "bodyprog/libsd.h"
#include "bodyprog/sound/sound_system.h"
#include "main/fileinfo.h"
#include "sh_log.h"
#include "pc_remix_sfx_data.h"

extern unsigned char* Pc_LangReadDiscFile(unsigned int sector, unsigned int size);
extern unsigned long  SpuWrite(unsigned char* addr, unsigned long size);
extern unsigned long  SpuSetTransferStartAddr(unsigned long addr);
extern int            Pc_Sd_AmbientState(u16* lastCmd);

#define RSFX_FIRST_VAB 4
#define RSFX_SLOTS     7
#define RSFX_CHARAS    64
#define RSFX_SFX_COUNT 420
#define RSFX_FILES     8
#define RSFX_EXT_BASE  0x80000
#define RSFX_EXT_END   0x200000
#define RSFX_EXT_REBUILDS 4 /* per map */

/* Each monster family's own sound ids (its character code and its data
 * tables: the nurse's g_NursePuppetSfxs, the air screamer's sfxVolumes_CE8). */
typedef struct
{
    s8  chara;
    u16 first, last;
} s_RsfxRange;

static const s_RsfxRange FAMILY_SFX[] = {
    { Chara_GreyChild, 1363, 1366 },     { Chara_Mumbler, 1363, 1366 },     { Chara_Stalker, 1363, 1366 },
    { Chara_LarvalStalker, 1429, 1429 }, { Chara_Creeper, 1424, 1428 },     { Chara_Groaner, 1406, 1414 },
    { Chara_Wormhead, 1406, 1414 },      { Chara_Romper, 1400, 1405 },      { Chara_PuppetNurse, 1506, 1519 },
    { Chara_PuppetDoctor, 1506, 1519 },  { Chara_AirScreamer, 1376, 1383 }, { Chara_AirScreamer, 1590, 1590 },
    { Chara_NightFlutter, 1376, 1383 },  { Chara_NightFlutter, 1590, 1590 }, { Chara_HangedScratcher, 1577, 1584 },
};

/* Ground-material footsteps (Player_FootstepSfxGet). Several are ambient-bank
 * sounds that monsters walking on the same ground play too: they're the map's
 * own surface sounds, so they always play from the map's own bank. */
static const u16 SURFACE_SFX[] = { 1311, 1312, 1313, 1330, 1331, 1332, 1346, 1388, 1389, 1543, 1557, 1565, 1600, 1608 };

typedef struct
{
    int cmd;
    u8* data; /* whole bank file, so a rebuild after an ambient change needs no disc read */
    u32 size;
} s_RsfxFile;

static u8*         s_hdr[RSFX_SLOTS];        /* rewritten VAB headers; libsd keeps pointing at them */
static s_RsfxFile  s_files[RSFX_FILES];
static u8          s_need[RSFX_CHARAS];      /* foreign types on this map */
static int         s_typeCmd[RSFX_CHARAS];   /* home bank per foreign type */
static s8          s_typeVab[RSFX_CHARAS];   /* libsd VAB id per type once packed, 0 = none */
static int         s_latchVab;               /* VAB id of the sound's emitter, 0 = none */
static u8          s_sfxVab[RSFX_SFX_COUNT]; /* VAB id a sound last keyed on from, 0 = its own */
static int         s_on;
static int         s_ext; /* sample memory past 512 KiB is real (see top) */
static int         s_built;
static const void* s_builtFor; /* the ambient bank the packing was placed after */
static s32         s_builtSize;
static u16         s_builtCmd;
static const s_SubCharacter* s_updating; /* the NPC whose AI update is running (npc_main.c) */
static int         s_extRebuilds;

static const s_RemixCharaBanks* banks_of(int chara)
{
    int i;
    for (i = 0; i < (int)ARRAY_SIZE(REMIX_CHARA_BANKS); i++)
        if (REMIX_CHARA_BANKS[i].chara == chara)
            return &REMIX_CHARA_BANKS[i];
    return NULL;
}

static int bank_has(const s_RemixCharaBanks* b, int cmd)
{
    int i;
    for (i = 0; b != NULL && i < b->count; i++)
        if (b->cmds[i] == cmd)
            return 1;
    return 0;
}

static int is_surface_sfx(int sfxId)
{
    int i;
    for (i = 0; i < (int)ARRAY_SIZE(SURFACE_SFX); i++)
        if (SURFACE_SFX[i] == sfxId)
            return 1;
    return 0;
}

static int slot_live(int vab)
{
    int s = vab - RSFX_FIRST_VAB;
    return s >= 0 && s < RSFX_SLOTS && s_hdr[s] != NULL && vab_h[vab].vh_addr_4 == (SD_VAB_H*)s_hdr[s];
}

static void slots_clear(void)
{
    int s;
    for (s = 0; s < RSFX_SLOTS; s++)
    {
        if (slot_live(RSFX_FIRST_VAB + s))
        {
            vab_h[RSFX_FIRST_VAB + s].vab_id_0  = NO_VALUE;
            vab_h[RSFX_FIRST_VAB + s].vh_addr_4 = NULL;
        }
        free(s_hdr[s]);
        s_hdr[s] = NULL;
    }
    memset(s_typeVab, 0, sizeof(s_typeVab));
    s_built = 0;
}

static const s_RsfxFile* file_get(int cmd)
{
    const s_AudioItemData* f;
    u8*                    data;
    int                    i, slot = -1;

    if (cmd < 160 || cmd >= 160 + 127)
        return NULL;
    for (i = 0; i < RSFX_FILES; i++)
        if (s_files[i].cmd == cmd)
            return &s_files[i];

    f = &g_AudioData[cmd - 160];
    if (f->typeIdx_0 != AudioType_Ambient || f->fileSize_4 <= f->vagDataOffset_2)
        return NULL;
    data = Pc_LangReadDiscFile((unsigned int)f->fileOffset_8, f->fileSize_4);
    if (data == NULL || data[0] != 0x70 || ((VabHdr*)data)->ps == 0 || ((VabHdr*)data)->ps > 128 ||
        f->vagDataOffset_2 < 0xA20 + ((VabHdr*)data)->ps * 0x200)
    {
        free(data);
        SH_DBG("[REMIX-SFX] bank %d could not be read from the disc", cmd);
        return NULL;
    }

    for (i = 0; i < RSFX_FILES && slot < 0; i++)
        if (s_files[i].cmd == 0)
            slot = i;
    if (slot < 0)
    {
        slot = 0;
        free(s_files[0].data);
    }
    s_files[slot].cmd  = cmd;
    s_files[slot].data = data;
    s_files[slot].size = f->fileSize_4;
    return &s_files[slot];
}

/* libsd finds VAG v at the sum of entries 0..v-1 of this u16 table (8-byte units). */
static u16* vag_table(u8* hdr)
{
    return (u16*)(hdr + (((VabHdr*)hdr)->ps << 9) + 0x820);
}

/* Mark the VAGs a type's sounds play in this bank (indexed like SdVoKeyOn). */
static void type_vags(const u8* hdr, int chara, u8* want)
{
    const SD_VAB_H* vh = (const SD_VAB_H*)hdr;
    int             k, id, t;

    for (k = 0; k < (int)ARRAY_SIZE(FAMILY_SFX); k++)
    {
        if (FAMILY_SFX[k].chara != chara)
            continue;
        for (id = FAMILY_SFX[k].first; id <= FAMILY_SFX[k].last; id++)
        {
            u16 vp   = g_Vab_InfoTable[id - Sfx_Base].vab_progIdx_2;
            int prog = vp & 0xFF;

            if ((vp >> 8) != AudioType_Ambient || is_surface_sfx(id) || prog >= vh->vab_h.ps)
                continue;
            for (t = 0; t < 16; t++)
            {
                int v = vh->vag_atr[prog * 16 + t].vag;
                if (v > 0 && v <= vh->vab_h.vs)
                    want[v] = 1;
            }
        }
    }
}

static u32 want_bytes(const u8* hdr, const u8* want)
{
    const u16* tbl = vag_table((u8*)hdr);
    u32        n   = 0;
    int        v;

    for (v = 1; v <= ((const VabHdr*)hdr)->vs && v < 256; v++)
        if (want[v])
            n += (u32)tbl[v] * 8;
    return n;
}

/* Pack the samples of every foreign type homed on `cmd` that still fits under
 * `limit` into SPU RAM at *spu, as libsd VAB `vab`. */
static int pack_bank(int cmd, int vab, u32* spu, u32 limit)
{
    const s_RsfxFile* f = file_get(cmd);
    u32               hdrSize, packed, srcOff = 0, dst = 0;
    u8                want[256], tryWant[256];
    u8                fits[RSFX_CHARAS];
    u8*               hdr;
    u8*               body;
    u16*              tbl;
    int               nv, v, c, any = 0;

    if (f == NULL)
        return 0;
    hdrSize = g_AudioData[cmd - 160].vagDataOffset_2;

    memset(want, 0, sizeof(want));
    memset(fits, 0, sizeof(fits));
    for (c = 0; c < RSFX_CHARAS; c++)
    {
        if (!s_need[c] || s_typeCmd[c] != cmd)
            continue;
        memcpy(tryWant, want, sizeof(want));
        type_vags(f->data, c, tryWant);
        packed = want_bytes(f->data, tryWant);
        if (packed == 0 || *spu + packed > limit)
        {
            SH_DBG("[REMIX-SFX] chara %d: %u bytes of samples from bank %d don't fit (%u free)", c, packed, cmd,
                   limit > *spu ? limit - *spu : 0);
            continue;
        }
        memcpy(want, tryWant, sizeof(want));
        fits[c] = 1;
        any     = 1;
    }
    if (!any)
        return 0;

    packed = want_bytes(f->data, want);
    hdr    = (u8*)malloc(hdrSize);
    body   = (u8*)malloc(packed);
    if (hdr == NULL || body == NULL)
    {
        free(hdr);
        free(body);
        return 0;
    }
    memcpy(hdr, f->data, hdrSize);
    tbl = vag_table(hdr);
    nv  = ((VabHdr*)hdr)->vs;
    if (nv > 255)
        nv = 255;
    for (v = 0; v <= nv; v++)
    {
        u32 len = (u32)tbl[v] * 8;
        if (v > 0 && want[v] && hdrSize + srcOff + len <= f->size)
        {
            memcpy(body + dst, f->data + hdrSize + srcOff, len);
            dst += len;
        }
        else
            tbl[v] = 0;
        srcOff += len;
    }
    ((VabHdr*)hdr)->fsize = 0xA20 + ((VabHdr*)hdr)->ps * 0x200 + dst;

    if (dst == 0 || SdVabFakeHead(hdr, (s16)vab, *spu) != vab)
    {
        free(body);
        free(hdr);
        return 0;
    }
    tone_adsr_mem((s16)vab);
    SpuSetTransferStartAddr(*spu);
    SpuWrite(body, dst);
    free(body);
    s_hdr[vab - RSFX_FIRST_VAB] = hdr;

    for (c = 0; c < RSFX_CHARAS; c++)
        if (fits[c])
        {
            s_typeVab[c] = (s8)vab;
            SH_DBG("[REMIX-SFX] chara %d plays from VAB %d", c, vab);
        }
    SH_DBG("[REMIX-SFX] bank %d: %u bytes of monster samples -> VAB %d at SPU 0x%X", cmd, dst, vab, *spu);
    *spu = (*spu + dst + 15) & ~15u;
    return 1;
}

/* Extended RAM: a home bank whole, at *spu. */
static int load_bank(int cmd, int vab, u32* spu, u32 limit)
{
    const s_RsfxFile* f = file_get(cmd);
    u32               hdrSize, body;
    u8*               hdr;
    int               c;

    if (f == NULL)
        return 0;
    hdrSize = g_AudioData[cmd - 160].vagDataOffset_2;
    body    = f->size - hdrSize;
    if (*spu + body > limit || (hdr = (u8*)malloc(hdrSize)) == NULL)
        return 0;
    memcpy(hdr, f->data, hdrSize);
    if (SdVabFakeHead(hdr, (s16)vab, *spu) != vab)
    {
        free(hdr);
        return 0;
    }
    tone_adsr_mem((s16)vab);
    SpuSetTransferStartAddr(*spu);
    SpuWrite(f->data + hdrSize, body);
    s_hdr[vab - RSFX_FIRST_VAB] = hdr;

    for (c = 0; c < RSFX_CHARAS; c++)
        if (s_need[c] && s_typeCmd[c] == cmd)
        {
            s_typeVab[c] = (s8)vab;
            SH_DBG("[REMIX-SFX] chara %d plays from VAB %d", c, vab);
        }
    SH_DBG("[REMIX-SFX] bank %d -> VAB %d at SPU 0x%X (%u bytes)", cmd, vab, *spu, body);
    *spu = (*spu + body + 15) & ~15u;
    return 1;
}

static int ambient_ready(u16* cmd)
{
    return !Pc_Sd_AmbientState(cmd) && vab_h[2].vab_id_0 == 2 && vab_h[2].vh_addr_4 != NULL &&
           vab_h[2].vb_start_addr_10 == D_800A9FDC[AudioType_Ambient] && vab_h[2].vb_size_14 > 0;
}

static int ambient_same(u16 cmd)
{
    return s_builtFor == (const void*)vab_h[2].vh_addr_4 && s_builtSize == vab_h[2].vb_size_14 && s_builtCmd == cmd;
}

static void build(u16 cmd)
{
    u32 spu   = s_ext ? RSFX_EXT_BASE : ((u32)vab_h[2].vb_start_addr_10 + (u32)vab_h[2].vb_size_14 + 15) & ~15u;
    u32 limit = s_ext ? RSFX_EXT_END : (u32)D_800A9FDC[AudioType_MusicBank];
    int cmds[RSFX_SLOTS], nCmds = 0, i, c;

    slots_clear();
    s_builtFor  = vab_h[2].vh_addr_4;
    s_builtSize = vab_h[2].vb_size_14;
    s_builtCmd  = cmd;
    s_built     = 1;

    for (c = 0; c < RSFX_CHARAS; c++)
    {
        if (!s_need[c])
            continue;
        for (i = 0; i < nCmds && cmds[i] != s_typeCmd[c]; i++)
            ;
        if (i == nCmds && nCmds < RSFX_SLOTS)
            cmds[nCmds++] = s_typeCmd[c];
    }
    for (i = 0; i < nCmds && spu < limit; i++)
    {
        if (s_ext)
            load_bank(cmds[i], RSFX_FIRST_VAB + i, &spu, limit);
        else
            pack_bank(cmds[i], RSFX_FIRST_VAB + i, &spu, limit);
    }
}

void Pc_RemixSfx_Off(void)
{
    s_updating    = NULL;
    s_extRebuilds = 0;
    slots_clear();
    memset(s_need, 0, sizeof(s_need));
    memset(s_sfxVab, 0, sizeof(s_sfxVab));
    s_latchVab = 0;
    s_on       = 0;
    s_ext      = 0;
}

/* Map load with remix enemies on. `types` are the monster types the pool may
 * spawn here; `ambientIdx` is the map's own ambientAudioIdx. */
void Pc_RemixSfx_OnMapLoad(const s8* types, int count, int ambientIdx)
{
    int curCmd = (ambientIdx >= 0 && ambientIdx < (int)ARRAY_SIZE(REMIX_AMBIENT_CMDS)) ? REMIX_AMBIENT_CMDS[ambientIdx] : 0;
    int i, k, c, any = 0;

    Pc_RemixSfx_Off();

    for (i = 0; i < count; i++)
    {
        const s_RemixCharaBanks* b = banks_of(types[i]);
        if (b == NULL || b->count == 0 || bank_has(b, curCmd) || types[i] < 0 || types[i] >= RSFX_CHARAS)
            continue;
        s_need[types[i]] = 1;
        any              = 1;
    }
    if (!any)
        return;

    /* Fewer home banks means fewer files and shared samples packed once. */
    for (c = 0; c < RSFX_CHARAS; c++)
    {
        const s_RemixCharaBanks* b = banks_of(c);
        int                      bestN = -1;

        if (!s_need[c])
            continue;
        for (k = 0; k < b->count; k++)
        {
            int o, n = 0;
            for (o = 0; o < RSFX_CHARAS; o++)
                n += s_need[o] && bank_has(banks_of(o), b->cmds[k]);
            if (n > bestN)
            {
                bestN        = n;
                s_typeCmd[c] = b->cmds[k];
            }
        }
        file_get(s_typeCmd[c]);
    }
    s_on  = 1;
    s_ext = SpuSetTransferStartAddr(RSFX_EXT_BASE) != 0;
    SH_DBG("[REMIX-SFX] monster samples go %s", s_ext ? "above 512 KiB"
                                                      : "in the ambient slot's free tail (for room for all of them: "
                                                        "the Remix launcher's \"More sound memory\", or the PsyCross patch)");
    if (s_ext)
        build(0);
}

void Pc_RemixSfx_Updating(const void* npc)
{
    s_updating = (const s_SubCharacter*)npc;
}

/* Sfx_With* / func_8005D* entry: latch the emitter when the sound is a foreign
 * monster's. Most play from the monster's own position; some from a point of
 * their own (the hanged scratcher's claws), which only the monster whose
 * update is running can have played. */
void Pc_RemixSfx_Emitter(const VECTOR3* pos)
{
    const u8* p = (const u8*)pos;
    const u8* a = (const u8*)&g_SysWork.npcs[0];
    const u8* b = (const u8*)&g_SysWork.npcs[ARRAY_SIZE(g_SysWork.npcs)];
    int       c;
    u16       cmd;

    s_latchVab = 0;
    if (!s_on)
        return;
    if (p >= a && p < b)
        c = g_SysWork.npcs[(p - a) / sizeof(s_SubCharacter)].model.charaId;
    else if (s_updating != NULL && pos != &g_SysWork.playerWork.player.position)
        c = s_updating->model.charaId;
    else
        return;
    if (c < 0 || c >= RSFX_CHARAS || !s_need[c])
        return;

    if (!s_ext)
    {
        if (!ambient_ready(&cmd))
            return;
        if (!s_built || !ambient_same(cmd))
            build(cmd);
    }
    else if (s_typeVab[c] != 0 && !slot_live(s_typeVab[c]) && s_extRebuilds < RSFX_EXT_REBUILDS)
    {
        /* Something closed or reused the VAB slot (Sd_AudioStop closes 0-4). */
        s_extRebuilds++;
        SH_DBG("[REMIX-SFX] VAB %d was taken over -- loading the monster banks again", s_typeVab[c]);
        build(0);
    }
    if (slot_live(s_typeVab[c]))
        s_latchVab = s_typeVab[c];
}

void Pc_RemixSfx_EmitterClear(void)
{
    s_latchVab = 0;
}

/* Key-on: the (VAB << 8 | program) to play `sfxId` from. */
u16 Pc_RemixSfx_VabPro(u16 sfxId, u16 vabPro)
{
    int idx = sfxId - Sfx_Base;

    if (idx <= 0 || idx >= RSFX_SFX_COUNT)
        return vabPro;
    if (s_latchVab != 0 && (vabPro >> 8) == AudioType_Ambient && !is_surface_sfx(sfxId))
    {
        s_sfxVab[idx] = (u8)s_latchVab;
        return (u16)((s_latchVab << 8) | (vabPro & 0xFF));
    }
    s_sfxVab[idx] = 0;
    return vabPro;
}

/* Key-off: the bank `sfxId` was last keyed on from, if a borrowed one. */
u16 Pc_RemixSfx_StopVabPro(u16 sfxId, u16 vabPro)
{
    int idx = sfxId - Sfx_Base;

    if (idx <= 0 || idx >= RSFX_SFX_COUNT || s_sfxVab[idx] == 0)
        return vabPro;
    return (u16)((s_sfxVab[idx] << 8) | (vabPro & 0xFF));
}

int Pc_RemixSfx_On(void)
{
    return s_on;
}
