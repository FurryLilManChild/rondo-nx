// rondo-nx dispatcher: finds the translated block for the current PC.
#include "rondo_xlat_rt.h"
#include <string.h>
#include <stdlib.h>

#include "xlat_table.inc"

unsigned long long RondoXlat::s_blocks_run = 0;
unsigned long long RondoXlat::s_misses = 0;
unsigned long long RondoXlat::s_instructions = 0;
bool RondoXlat::s_wide_sprites = false;

// Widescreen sprite patch. The game's per-piece sprite routine (loaded into bank 72 in the
// stages checked, at $8100-$813F) skips any piece whose chip x (screen x + 32, 16-bit in
// zero page $04 high / $05 low) is not in [16 or 0, 287]. With widescreen on, pieces that
// reach into the side areas are kept instead, by steering its three decisions:
//   $8127  high byte negative  -> keep if x >= -44 (chip), that is screen x >= -76
//   $812A  high byte positive  -> keep if x < 0x14C (screen x < 300)
//   $8138  high byte zero      -> always keep
// Kept pieces continue at $813F, skipped ones at $818C (as in the original). The code bytes are
// checked first, since other overlays use the same addresses.
static const u8 k_spr_sig[] = {
    0xF0, 0x13, 0x10, 0x03, 0x4C, 0x8C, 0x81, 0xA5, 0x04, 0xC9, 0x01, 0xD0, 0x5C, 0xA5, 0x05, 0xC9,
    0x20, 0xB0, 0x56, 0x80, 0x07, 0xA5, 0x05, 0xDD, 0xA1, 0x81, 0x90, 0x4D};   // $8123-$813E
static const int kWideX = 44;   // keep in step with RondoWide::kExtra

static inline bool wide_sprite_hook(GeargrafxCore* g, HuC6280* c, u16 pc, u8 bank, const u8* src)
{
    if (pc != 0x8127 && pc != 0x812A && pc != 0x8138) return false;
    if (memcmp(src + 0x0123, k_spr_sig, sizeof(k_spr_sig)) != 0) return false;
    const u8* zp = g->GetMemory()->GetMemoryMap()[0xF8];
    int hi = zp[0x04], lo = zp[0x05];
    int x = (hi << 8 | lo);
    if (x >= 0x8000) x -= 0x10000;                  // chip x = screen x + 32
    bool keep;
    if (pc == 0x8138) keep = true;
    else if (pc == 0x8127) keep = x >= -kWideX;      // screen x >= -(kWideX + 32): a 32-wide piece still reaches in
    else keep = x < 256 + 32 + kWideX;
    c->XlatSetPC(keep ? 0x813F : 0x818C);
    (void)bank;
    return true;
}

static int* s_head[256];
static int* s_next;

void RondoXlat::Init()
{
    for (int b = 0; b < 256; b++)
        s_head[b] = NULL;
    s_next = (int*)malloc(sizeof(int) * (k_xlat_count > 0 ? k_xlat_count : 1));
    for (int i = k_xlat_count - 1; i >= 0; i--)
    {
        const XlatEntry& e = k_xlat_entries[i];
        if (!s_head[e.bank])
        {
            s_head[e.bank] = (int*)malloc(sizeof(int) * 0x2000);
            for (int k = 0; k < 0x2000; k++)
                s_head[e.bank][k] = -1;
        }
        s_next[i] = s_head[e.bank][e.off];
        s_head[e.bank][e.off] = i;
    }
}

int RondoXlat::Dispatch(GeargrafxCore* g)
{
    HuC6280* c = g->GetHuC6280();
    if (c->m_transfer_state != 0)
        return -1;
    u16 pc = c->XlatPC();
    Memory* mem = g->GetMemory();
    u8 bank = mem->GetMpr(pc >> 13);
    int* head = s_head[bank];
    if (!head)
    {
        s_misses++;
        return -1;
    }
    u16 off = pc & 0x1FFF;
    const u8* src = mem->GetMemoryMap()[bank];
    if (s_wide_sprites && (pc & 0xE000) == 0x8000 && wide_sprite_hook(g, c, pc, bank, src))
    {
        s_misses++;
        return -1;                                  // the interpreter continues at the new PC
    }
    for (int i = head[off]; i >= 0; i = s_next[i])
    {
        const XlatEntry& e = k_xlat_entries[i];
        bool match;
        if (!e.mask)
            match = memcmp(src + off, e.sig, e.len) == 0;
        else
        {
            match = true;
            for (int k = 0; k < e.len; k++)
                if ((src[off + k] & e.mask[k]) != e.sig[k]) { match = false; break; }
        }
        if (match)
        {
            s_blocks_run++;
            return e.fn(g, c, (u16)(pc & 0xE000));
        }
    }
    s_misses++;
    return -1;
}

static int dispatch_thunk(GeargrafxCore* g) { return RondoXlat::Dispatch(g); }

void RondoXlat::SetEnabled(bool e)
{
    (void)e;
}

void rondo_xlat_attach(GeargrafxCore* core, bool enabled)
{
    static bool inited = false;
    if (!inited)
    {
        RondoXlat::Init();
        inited = true;
    }
    core->SetXlat(enabled ? dispatch_thunk : NULL);
}
