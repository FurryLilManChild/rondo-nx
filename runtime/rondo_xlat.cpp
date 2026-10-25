// rondo-nx dispatcher: finds the translated block for the current PC.
#include "rondo_xlat_rt.h"
#include <string.h>
#include <stdlib.h>

#include "xlat_table.inc"

unsigned long long RondoXlat::s_blocks_run = 0;
unsigned long long RondoXlat::s_misses = 0;
unsigned long long RondoXlat::s_instructions = 0;

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
    for (int i = head[off]; i >= 0; i = s_next[i])
    {
        const XlatEntry& e = k_xlat_entries[i];
        if (memcmp(src + off, e.sig, e.len) == 0)
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
