#include "rondo_trace.h"
#include "memory.h"
#include "memory_inline.h"
#include "huc6280_timing.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <map>
#include <string>

namespace {

struct Snapshot
{
    u8 bank;
    u8 page_mask;
    u64 hash;
    u8 data[0x2000];
    u8 exec[0x400];
};

const int k_max_snapshots = 8000;

std::vector<Snapshot*> g_snaps;
std::map<std::pair<u8, u64>, int> g_index;
int g_current[256];
bool g_enabled = false;
bool g_dirty = false;
std::string g_dir;
std::string g_session;
FILE* g_cdlog = NULL;
u32 g_exec_count = 0;
u32 g_unique_exec = 0;
} // namespace
std::map<u32,u32> g_rt_hist; bool g_rt_hist_on = false;
namespace {

u64 fnv(const u8* p, int n)
{
    u64 h = 1469598103934665603ULL;
    for (int i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

int take_snapshot(u8 bank, const u8* src)
{
    u64 h = fnv(src, 0x2000);
    std::pair<u8, u64> key(bank, h);
    std::map<std::pair<u8, u64>, int>::iterator it = g_index.find(key);
    if (it != g_index.end())
        return it->second;
    if ((int)g_snaps.size() >= k_max_snapshots)
        return -1;
    Snapshot* s = new Snapshot();
    s->bank = bank;
    s->page_mask = 0;
    s->hash = h;
    memcpy(s->data, src, 0x2000);
    memset(s->exec, 0, sizeof(s->exec));
    g_snaps.push_back(s);
    int id = (int)g_snaps.size() - 1;
    g_index[key] = id;
    g_dirty = true;
    return id;
}

} // namespace

void rondo_trace_init(const char* out_dir)
{
    g_dir = out_dir ? out_dir : ".";
    for (int i = 0; i < 256; i++) g_current[i] = -1;
    time_t t = time(NULL);
    struct tm* lt = localtime(&t);
    char name[64];
    strftime(name, sizeof(name), "rondo_trace_%Y%m%d_%H%M%S", lt);
    g_session = name;
    std::string cd = g_dir + "/" + g_session + "_cdread.txt";
    g_cdlog = fopen(cd.c_str(), "w");
    if (g_cdlog)
        fprintf(g_cdlog, "# pc_bank count addr_lo addr_hi rec_h rec_m rec_l mode  (zero page $F8-$FF at cd_read entry)\n");
    g_enabled = true;
}

void rondo_trace_instruction(Memory* memory, u16 pc)
{
    if (!g_enabled)
        return;

    u8 page = pc >> 13;
    u8 bank = memory->GetMpr(page);
    if (bank == 0xFF)
        return;

    u8** map = memory->GetMemoryMap();
    const u8* src = map[bank];
    if (!src)
        return;

    u16 off = pc & 0x1FFF;
    u8 opcode = src[off];
    int size = k_huc6280_opcode_sizes[opcode];
    if (size < 1) size = 1;
    if (off + size > 0x2000) size = 0x2000 - off;

    // Work RAM holds small self-patching transfer routines (TIA/TIN/TII with
    // rewritten operands). Only the opcode matters there, so compare one byte.
    if (bank >= 0xF8)
        size = 1;

    int id = g_current[bank];
    if (id < 0 || memcmp(g_snaps[id]->data + off, src + off, size) != 0)
    {
        id = take_snapshot(bank, src);
        g_current[bank] = id;
        if (id < 0)
            return;
    }

    Snapshot* s = g_snaps[id];
    u8 bit = 1 << (off & 7);
    if (!(s->exec[off >> 3] & bit))
    {
        s->exec[off >> 3] |= bit;
        g_unique_exec++;
        g_dirty = true;
    }
    if (!(s->page_mask & (1 << page)))
    {
        s->page_mask |= (1 << page);
        g_dirty = true;
    }
    g_exec_count++;
    if (g_rt_hist_on) g_rt_hist[((u32)bank << 16) | pc]++;

    // System Card cd_read entry ($E009 with BIOS bank 0 in MPR7)
    if (pc == 0xE009 && bank == 0x00 && g_cdlog)
    {
        const u8* zp = map[memory->GetMpr(1)];
        if (zp)
        {
            fprintf(g_cdlog, "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                memory->GetMpr(4), zp[0xF8], zp[0xFA], zp[0xFB], zp[0xFC], zp[0xFD], zp[0xFE], zp[0xFF]);
            fflush(g_cdlog);
        }
    }
}

void rondo_trace_save(void)
{
    if (!g_enabled || !g_dirty)
        return;
    std::string path = g_dir + "/" + g_session + ".rtr";
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f)
        return;
    u32 n = (u32)g_snaps.size();
    fwrite("RTRC", 1, 4, f);
    u32 ver = 1;
    fwrite(&ver, 4, 1, f);
    fwrite(&n, 4, 1, f);
    for (u32 i = 0; i < n; i++)
    {
        Snapshot* s = g_snaps[i];
        u8 hdr[4] = { s->bank, s->page_mask, 0, 0 };
        fwrite(hdr, 1, 4, f);
        fwrite(&s->hash, 8, 1, f);
        fwrite(s->data, 1, 0x2000, f);
        fwrite(s->exec, 1, 0x400, f);
    }
    fclose(f);
    remove(path.c_str());
    rename(tmp.c_str(), path.c_str());
    g_dirty = false;
}

void rondo_trace_status(char* buf, int len)
{
    snprintf(buf, len, "Trace: %u code images, %u instructions found%s",
        (unsigned)g_snaps.size(), (unsigned)g_unique_exec,
        (int)g_snaps.size() >= k_max_snapshots ? " (FULL)" : "");
}
