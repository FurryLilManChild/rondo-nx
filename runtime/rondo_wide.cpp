#include "rondo_wide.h"
#include "geargrafx.h"
#include "huc6260.h"
#include "huc6270.h"
#include "memory_inline.h"
#include <string.h>

namespace RondoWide
{
int s_row_offset = 0;
int s_sprite_row_offset = -3;   // measured: sprite raster lines run 3 behind picture rows

static inline uint32_t to_u32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

// Camera (an object in the game's tables, work RAM bank F8):
//   1E08 / 1E10  camera x, low / high byte (this frame's value; the screen shows it next frame)
//   1E41 / 1E4A  camera x as scrolled on screen this frame
//   1E96         camera screen number (x / 256, low 4 bits)
//   1EBE         left limit: screen number (the camera stops at pixel 0 of it)
//   1EC6 / 1EC7  right limit: screen number / pixel for the camera's left edge
// Found from the game's camera code (bank 6A, around $6879 and $6971).
void RoomExtent(GeargrafxCore* core, int* left, int* right)
{
    const u8* w = core->GetMemory()->GetMemoryMap()[0xF8];
    int cs = w[0x1E96] & 15, clo = w[0x1E08];
    int dl = ((cs - w[0x1EBE]) & 15) * 256 + clo;
    int dr = ((w[0x1EC6] - cs) & 15) * 256 + w[0x1EC7] - clo;
    // The picture shows the previous camera position: adjust by how far it has moved since.
    int cur = w[0x1E08] | (w[0x1E10] << 8);
    int shown = w[0x1E41] | (w[0x1E4A] << 8);
    int moved = cur - shown;
    if (moved > 64 || moved < -64) moved = 0;     // a jump (new room): don't trust it
    dl -= moved;
    dr += moved;
    *left = dl < 0 ? 0 : dl;
    *right = dr < 0 ? 0 : dr;
}

void Capture(GeargrafxCore* core, Snapshot* s, bool side_ok)
{
    RoomExtent(core, &s->room_left, &s->room_right);
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
    memcpy(s->sat, vdc->GetSAT(), sizeof(s->sat));
    s->sprite_mode1 = (((R[9] >> 2) & 3) == 1);
    memcpy(s->colors, vce->GetColorTable(), sizeof(s->colors));
    s->pal888 = vce->GetPalette888();
    s->side_ok = side_ok;
}

// View of one frame's tile map and per-line scroll (live from the chip, or a snapshot).
struct Map
{
    const u16* vram;
    const u16* sat;
    int mode1;
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
        sat = vdc->GetSAT();
        mode1 = (((R[9] >> 2) & 3) == 1);
    }
    void Load(const Snapshot& s) { vram = s.vram; sat = s.sat; mode1 = s.sprite_mode1; lx = s.line_x; ly = s.line_y; lok = s.line_ok; mapw = s.mapw; maph = s.maph; }

    // Sprites on one raster line, in priority order (lowest SAT index first, as the chip does).
    struct LineSprite { int x; u16 flags; u16 p[4]; };
    int Sprites(int line, LineSprite* out) const
    {
        static const int heights[4] = {16, 32, 64, 64};
        static const int wmask[2] = {0xFFFF, 0xFFFE};
        static const int hmask[4] = {0xFFFF, 0xFFFD, 0xFFF9, 0xFFF9};
        int n = 0;
        for (int i = 0; i < 64; i++)
        {
            const u16* e = sat + i * 4;
            int sy = (e[0] & 0x3FF) - 64;
            u16 flags = e[3];
            int cgy = (flags >> 12) & 3, h = heights[cgy];
            if (line < sy || line >= sy + h) continue;
            int ry = line - sy;
            if (flags & 0x8000) ry = h - 1 - ry;
            int cgx = (flags >> 8) & 1;
            int pattern = ((e[2] >> 1) & 0x3FF) & wmask[cgx] & hmask[cgy];
            int m1 = mode1 ? (e[2] & 1) << 5 : 0;
            int base = (pattern << 6) + (ry >> 4) * 128 + (ry & 15) + m1;
            int x = (e[1] & 0x3FF) - 32;
            if (x >= 512) x -= 1024;                // the chip's x wraps; treat as left of the screen
            for (int half = 0; half <= cgx; half++)
            {
                int a = base + (cgx ? ((flags & 0x0800) ? (half ? 0 : 64) : (half ? 64 : 0)) : 0);
                LineSprite& ls = out[n++];
                ls.x = x + half * 16;
                ls.flags = flags;
                ls.p[0] = vram[a & 0x7FFF]; ls.p[1] = vram[(a + 16) & 0x7FFF];
                ls.p[2] = mode1 ? 0 : vram[(a + 32) & 0x7FFF]; ls.p[3] = mode1 ? 0 : vram[(a + 48) & 0x7FFF];
            }
        }
        return n;
    }
    // Colour index for screen x given the line's sprites and the background index there.
    static inline int SpriteAt(const LineSprite* sp, int n, int sx, int bg)
    {
        for (int i = 0; i < n; i++)
        {
            int dx = sx - sp[i].x;
            if (dx < 0 || dx > 15) continue;
            int b = (sp[i].flags & 0x0800) ? dx : 15 - dx;
            int pix = ((sp[i].p[0] >> b) & 1) | (((sp[i].p[1] >> b) & 1) << 1) | (((sp[i].p[2] >> b) & 1) << 2) | (((sp[i].p[3] >> b) & 1) << 3);
            if (!pix) continue;
            if (!(sp[i].flags & 0x0080) && (bg & 0x0F)) return -2;   // behind an opaque background pixel
            return 256 + (sp[i].flags & 0x0F) * 16 + pix;
        }
        return -1;
    }
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

int SpriteIndex(GeargrafxCore* core, int x, int y)
{
    static Map m;
    m.Load(core);
    int li = y + s_row_offset;
    int bg = m.LineOk(li) ? m.Index(li, x) : 0;
    static Map::LineSprite sp[128];
    int n = m.Sprites(y + s_sprite_row_offset, sp);
    return Map::SpriteAt(sp, n, x, bg);
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
static void compose(const Map& m, const u8* pal888, const u16* ct, const uint8_t* frame, int w, int h, uint8_t* out, bool side_ok, int hud_rows,
                    int room_left, int room_right)
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
        static Map::LineSprite sp[128];
        int nsp = m.Sprites(y + s_sprite_row_offset, sp);
        for (int x = 0; x < w; x++)
        {
            int idx = m.Index(li, x);
            // Skip pixels a sprite covers: their colour is the sprite's, not the background's
            // (a big sprite such as a tree would otherwise teach its colours to the background).
            if (nsp && Map::SpriteAt(sp, nsp, x, idx) >= 256) continue;
            uint32_t px = src[x];
            if (seen[idx] != stamp) { seen[idx] = stamp; cand[idx] = px; votes[idx] = 1; }
            else if (cand[idx] == px) votes[idx]++;
            else if (--votes[idx] <= 0) { cand[idx] = px; votes[idx] = 1; }
        }
        for (int i = 0; i < kExtra; i++)
        {
            // left side: screen x = i - kExtra, that is kExtra - i pixels past the left edge
            if (kExtra - i <= room_left)
            {
                int a = m.Index(li, i - kExtra);
                int spi = nsp ? Map::SpriteAt(sp, nsp, i - kExtra, a) : -1;
                if (spi >= 256) dst[i] = to_u32(pal888 + (ct[spi] & 0x1FF) * 4);
                else dst[i] = seen[a] == stamp ? cand[a] : to_u32(pal888 + (ct[a] & 0x1FF) * 4);
            }
            else
                dst[i] = 0xFF000000u;
            // right side: screen x = w + i, that is i + 1 pixels past the right edge
            if (i < room_right)
            {
                int b = m.Index(li, w + i);
                int spi = nsp ? Map::SpriteAt(sp, nsp, w + i, b) : -1;
                if (spi >= 256) dst[kExtra + w + i] = to_u32(pal888 + (ct[spi] & 0x1FF) * 4);
                else dst[kExtra + w + i] = seen[b] == stamp ? cand[b] : to_u32(pal888 + (ct[b] & 0x1FF) * 4);
            }
            else
                dst[kExtra + w + i] = 0xFF000000u;
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
    int rl = kExtra, rr = kExtra;
    if (side_ok) RoomExtent(core, &rl, &rr);
    compose(m, vce->GetPalette888(), vce->GetColorTable(), frame, w, h, out, side_ok, hud_rows, rl, rr);
}

void Compose(const Snapshot& snap, const uint8_t* frame, int w, int h, uint8_t* out, int hud_rows)
{
    static Map m;
    m.Load(snap);
    compose(m, snap.pal888, snap.colors, frame, w, h, out, snap.side_ok, hud_rows, snap.room_left, snap.room_right);
}
}
