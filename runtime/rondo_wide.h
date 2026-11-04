// rondo-nx widescreen: draws extra background columns on both sides of the
// 256-pixel picture from the video chip's tile map, using the scroll position
// the game set for each line. Sprites are not drawn in the side areas (yet).
#ifndef RONDO_WIDE_H
#define RONDO_WIDE_H
#include <stdint.h>

class GeargrafxCore;

namespace RondoWide
{
    // Extra pixels on each side for 16:9 (256 * (16/9) / (4/3) = 341.3, so 43 each side; 44 keeps it even).
    const int kExtra = 44;

    // Row offset between the picture and the chip's per-line scroll table (measured: 0).
    extern int s_row_offset;

    // Background colour index (palette * 16 + pixel, 0 = transparent) at screen x (may be
    // outside 0..255) on picture row y, or -1 when that line has no background this frame.
    int BgIndex(GeargrafxCore* core, int x, int y);

    // Same, as RGBA from the end-of-frame colour table.
    bool BgPixel(GeargrafxCore* core, int x, int y, uint32_t* rgba);

    // Everything the side renderer needs from one frame, copied so the picture can be
    // built on another thread while emulation carries on.
    struct Snapshot
    {
        uint16_t vram[0x8000];
        uint16_t colors[512];
        uint16_t line_x[263], line_y[263];
        uint8_t line_ok[263];
        int mapw = 0, maph = 0;
        const uint8_t* pal888 = 0;     // the emulator's static RGBA table for 9-bit colours
        bool side_ok = false;
    };
    void Capture(GeargrafxCore* core, Snapshot* snap, bool side_ok);
    void Compose(const Snapshot& snap, const uint8_t* frame, int w, int h, uint8_t* out, int hud_rows);

    // Builds the wide picture: out is (w + 2 * kExtra) x h RGBA. The middle is the
    // emulator's picture unchanged. side_ok: draw the side areas (gameplay), else black.
    // hud_rows: picture rows at the top to keep black in the side areas.
    void Compose(GeargrafxCore* core, const uint8_t* frame, int w, int h, uint8_t* out, bool side_ok, int hud_rows);
}

#endif
