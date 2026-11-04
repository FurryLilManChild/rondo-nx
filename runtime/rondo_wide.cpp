#include "rondo_wide.h"
#include "geargrafx.h"
#include "huc6260.h"
#include "huc6270.h"
#include <string.h>

namespace RondoWide
{
int s_row_offset = 0;

static inline uint32_t to_u32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

void Capture(GeargrafxCore* core, Snapshot* s, bool side_ok)
{
    HuC6270* vdc = core->GetHuC6270_1();
    HuC6260* vce = core->GetHuC6260();
    u32 frame;
    HuC6270::HuC6270_Line_Scroll* ls = vdc->GetLineScroll(&frame);
    for (int i = 0; i < 263; i++) { s->line_x[i] = ls[i].x; s->line_y[i] = ls[i].y; s->line_ok[i] = ls[i].frame == frame; }
    u16* R = vdc->GetState()->R;
    int mwr = (R[9] >> 4) & 7;
    s->mapw = (mwr & 3) == 0 ? 32 : ((mwr & 3) == 1 ? 64 : 128);
    s->maph = (mwr & 4) ? 64 : 32;
    memcpy(s->vram, vdc->GetVRAM(), sizeof(s->vram));
    memcpy(s->colors, vce->GetColorTable(), sizeof(s->colors));
    s->pal888 = vce->GetPalette888();
    s->side_ok = side_ok;
}

// View of one frame's tile map and per-line scroll (live from the chip, or a snapshot).
struct Map
{
    const u16* vram;
    const u16* lx; const u16* ly; const u8* lok;
    int mapw, maph;
    u16 lx_buf[263], ly_buf[263]; u8 lok_buf[263];
    void Load(GeargrafxCore* core)
    {
        HuC6270* vdc = core->GetHuC6270_1();
        u32 frame;
        HuC6270::HuC6270_Line_Scroll* ls = vdc->GetLineScroll(&frame);
        for (int i = 0; i < 263; i++) { lx_buf[i] = ls[i].x; ly_buf[i] = ls[i].y; lok_buf[i] = ls[i].frame == frame; }
        lx = lx_buf; ly = ly_buf; lok = lok_buf;
        u16* R = vdc->GetState()->R;
        int mwr = (R[9] >> 4) & 7;
        mapw = (mwr & 3) == 0 ? 32 : ((mwr & 3) == 1 ? 64 : 128);
        maph = (mwr & 4) ? 64 : 32;
        vram = vdc->GetVRAM();
    }
    void Load(const Snapshot& s) { vram = s.vram; lx = s.line_x; ly = s.line_y; lok = s.line_ok; mapw = s.mapw; maph = s.maph; }
    // Colour index (palette * 16 + pixel, 0 = transparent) at screen x on line li.
    inline int Index(int li, int sx) const
    {
        int wx = (lx[li] + sx) & (mapw * 8 - 1);
        int wy = ly[li] & (maph * 8 - 1);
        u16 bat = vram[((wy >> 3) * mapw + (wx >> 3)) & 0x7FFF];
        int tile = bat & 0x7FF, pal = bat >> 12;
        int row = wy & 7;
        u16 p01 = vram[(tile * 16 + row) & 0x7FFF];
        u16 p23 = vram[(tile * 16 + row + 8) & 0x7FFF];
        int b = 7 - (wx & 7);
        int pix = ((p01 >> b) & 1) | (((p01 >> (b + 8)) & 1) << 1) | (((p23 >> b) & 1) << 2) | (((p23 >> (b + 8)) & 1) << 3);
        return pix ? (pal * 16 + pix) : 0;
    }
    inline bool LineOk(int li) const { return li >= 0 && li < 263 && lok[li]; }
};

int BgIndex(GeargrafxCore* core, int x, int y)
{
    Map m;
    m.Load(core);
    int li = y + s_row_offset;
    if (!m.LineOk(li)) return -1;
    return m.Index(li, x);
}

bool BgPixel(GeargrafxCore* core, int x, int y, uint32_t* rgba)
{
    int idx = BgIndex(core, x, y);
    if (idx < 0) return false;
    HuC6260* vce = core->GetHuC6260();
    *rgba = to_u32(vce->GetPalette888() + (vce->GetColorTable()[idx] & 0x1FF) * 4);
    return true;
}

// The game rewrites palette colours partway down the screen (sky gradients and the like), so the
// end-of-frame colour table is wrong for many lines. Each row learns its real colours from the
// visible middle of the same row: for every colour index seen there, the most common picture colour.
static void compose(const Map& m, const u8* pal888, const u16* ct, const uint8_t* frame, int w, int h, uint8_t* out, bool side_ok, int hud_rows)
{
    const int ow = w + 2 * kExtra;
    if (m.mapw * 8 <= w) side_ok = false;     // the map is no wider than the screen: nothing to show
    static uint32_t cand[256];
    static int votes[256];
    static uint16_t seen[256];
    static uint16_t stamp = 0;
    for (int y = 0; y < h; y++)
    {
        uint32_t* dst = (uint32_t*)(out + (size_t)y * ow * 4);
        const uint32_t* src = (const uint32_t*)(frame + (size_t)y * w * 4);
        memcpy(dst + kExtra, src, (size_t)w * 4);
        int li = y + s_row_offset;
        if (!side_ok || y < hud_rows || !m.LineOk(li))
        {
            for (int i = 0; i < kExtra; i++) { dst[i] = 0xFF000000u; dst[kExtra + w + i] = 0xFF000000u; }
            continue;
        }
        if (++stamp == 0) { memset(seen, 0, sizeof(seen)); stamp = 1; }
        for (int x = 0; x < w; x++)
        {
            int idx = m.Index(li, x);
            uint32_t px = src[x];
            if (seen[idx] != stamp) { seen[idx] = stamp; cand[idx] = px; votes[idx] = 1; }
            else if (cand[idx] == px) votes[idx]++;
            else if (--votes[idx] <= 0) { cand[idx] = px; votes[idx] = 1; }
        }
        for (int i = 0; i < kExtra; i++)
        {
            int a = m.Index(li, i - kExtra);
            dst[i] = seen[a] == stamp ? cand[a] : to_u32(pal888 + (ct[a] & 0x1FF) * 4);
            int b = m.Index(li, w + i);
            dst[kExtra + w + i] = seen[b] == stamp ? cand[b] : to_u32(pal888 + (ct[b] & 0x1FF) * 4);
        }
    }
}
}

namespace RondoWide
{
void Compose(GeargrafxCore* core, const uint8_t* frame, int w, int h, uint8_t* out, bool side_ok, int hud_rows)
{
    HuC6260* vce = core->GetHuC6260();
    static Map m;
    m.Load(core);
    compose(m, vce->GetPalette888(), vce->GetColorTable(), frame, w, h, out, side_ok, hud_rows);
}

void Compose(const Snapshot& snap, const uint8_t* frame, int w, int h, uint8_t* out, int hud_rows)
{
    static Map m;
    m.Load(snap);
    compose(m, snap.pal888, snap.colors, frame, w, h, out, snap.side_ok, hud_rows);
}
}
