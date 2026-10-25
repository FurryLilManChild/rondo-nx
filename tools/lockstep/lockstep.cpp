// Lockstep checker: runs an interpreted core and a translated core side by
// side with identical input and compares their full state after every frame.
// usage: lockstep <cue> <bios> <frames> <script>
// script: same format as the headless runner (press/release/cheat/load/poke)
#include "geargrafx.h"
#include "memory_inline.h"
#include "huc6270.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <chrono>

bool g_mcp_stdio_mode = false;
void rondo_xlat_attach(GeargrafxCore* core, bool enabled);
#include "rondo_xlat_rt.h"

struct Ev { int frame; std::string cmd, arg; };

static GG_Keys key_of(const std::string& s)
{
    if (s == "I") return GG_KEY_I; if (s == "II") return GG_KEY_II;
    if (s == "SELECT") return GG_KEY_SELECT; if (s == "RUN") return GG_KEY_RUN;
    if (s == "UP") return GG_KEY_UP; if (s == "DOWN") return GG_KEY_DOWN;
    if (s == "LEFT") return GG_KEY_LEFT; if (s == "RIGHT") return GG_KEY_RIGHT;
    return GG_KEY_NONE;
}

static void apply(GeargrafxCore* core, const Ev& e)
{
    const std::string& c = e.cmd;
    if (c == "press") core->KeyPressed(GG_CONTROLLER_1, key_of(e.arg));
    else if (c == "release") core->KeyReleased(GG_CONTROLLER_1, key_of(e.arg));
    else if (c == "cheat") core->SetCheat(e.arg.c_str());
    else if (c == "load") core->LoadState(e.arg.c_str());
    else if (c == "poke")
    {
        unsigned b, o; char hex[300];
        sscanf(e.arg.c_str(), "%x %x %299s", &b, &o, hex);
        u8* m = core->GetMemory()->GetMemoryMap()[b];
        for (unsigned k = 0; hex[k * 2] && hex[k * 2 + 1]; k++) { unsigned v; sscanf(hex + k * 2, "%2x", &v); m[o + k] = (u8)v; }
    }
}

static int compare(GeargrafxCore* a, GeargrafxCore* b, const u8* fa, const u8* fb, int fbsize,
                   const s16* sa, const s16* sb, int na, int nb, int frame)
{
    HuC6280::HuC6280_State* x = a->GetHuC6280()->GetState();
    HuC6280::HuC6280_State* y = b->GetHuC6280()->GetState();
    int bad = 0;
    if (x->PC->GetValue() != y->PC->GetValue() || x->A->GetValue() != y->A->GetValue() ||
        x->X->GetValue() != y->X->GetValue() || x->Y->GetValue() != y->Y->GetValue() ||
        x->S->GetValue() != y->S->GetValue() || x->P->GetValue() != y->P->GetValue())
    {
        printf("frame %d: CPU differs: PC %04X/%04X A %02X/%02X X %02X/%02X Y %02X/%02X S %02X/%02X P %02X/%02X\n", frame,
            x->PC->GetValue(), y->PC->GetValue(), x->A->GetValue(), y->A->GetValue(), x->X->GetValue(), y->X->GetValue(),
            x->Y->GetValue(), y->Y->GetValue(), x->S->GetValue(), y->S->GetValue(), x->P->GetValue(), y->P->GetValue());
        bad++;
    }
    u8** ma = a->GetMemory()->GetMemoryMap();
    u8** mb = b->GetMemory()->GetMemoryMap();
    const int banks[] = {0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,0x7F,
                         0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0xF8};
    for (unsigned i = 0; i < sizeof(banks) / sizeof(banks[0]); i++)
    {
        int k = banks[i];
        if (ma[k] && mb[k] && memcmp(ma[k], mb[k], 0x2000) != 0)
        {
            int off = 0;
            while (ma[k][off] == mb[k][off]) off++;
            printf("frame %d: RAM bank %02X differs at +%04X (%02X vs %02X)\n", frame, k, off, ma[k][off], mb[k][off]);
            bad++;
        }
    }
    if (memcmp(a->GetHuC6270_1()->GetVRAM(), b->GetHuC6270_1()->GetVRAM(), 0x10000) != 0)
    {
        printf("frame %d: VRAM differs\n", frame);
        bad++;
    }
    if (memcmp(fa, fb, fbsize) != 0)
    {
        printf("frame %d: picture differs\n", frame);
        bad++;
    }
    if (na != nb || memcmp(sa, sb, na * sizeof(s16)) != 0)
    {
        printf("frame %d: audio differs\n", frame);
        bad++;
    }
    return bad;
}

int main(int argc, char** argv)
{
    if (argc < 5) { printf("usage: lockstep cue bios frames script\n"); return 1; }
    int frames = atoi(argv[3]);
    std::vector<Ev> evs;
    FILE* sf = fopen(argv[4], "r");
    char line[512];
    while (sf && fgets(line, sizeof(line), sf))
    {
        char cmd[64] = {0}, arg[400] = {0}; int fr;
        if (line[0] == '#') continue;
        int n = sscanf(line, "%d %63s %399[^\n]", &fr, cmd, arg);
        if (n >= 2) evs.push_back({fr, cmd, n == 3 ? arg : ""});
    }
    if (sf) fclose(sf);

    GeargrafxCore* core[2];
    for (int i = 0; i < 2; i++)
    {
        core[i] = new GeargrafxCore();
        core[i]->Init(NULL, GG_PIXEL_RGBA8888);
        if (!core[i]->LoadBios(argv[2], true) || !core[i]->LoadMedia(argv[1])) { printf("load failed\n"); return 1; }
    }
    rondo_xlat_attach(core[1], true);

    // Benchmark mode: BENCH=0 runs only the interpreter, BENCH=1 only the translated core.
    const char* bench = getenv("BENCH");
    if (bench)
    {
        int which = atoi(bench) ? 1 : 0;
        const int fbs = 2048 * 512 * 4;
        std::vector<u8> fbb(fbs);
        std::vector<s16> sb(48000);
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < frames; f++)
        {
            for (size_t i = 0; i < evs.size(); i++)
                if (evs[i].frame == f) apply(core[which], evs[i]);
            int n = 0;
            core[which]->RunToVBlank(fbb.data(), sb.data(), &n);
        }
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("bench %s: %d frames in %.2fs = %.0f fps\n", which ? "translated" : "interpreter", frames, secs, frames / secs);
        return 0;
    }

    const int fbsize = 2048 * 512 * 4;
    std::vector<u8> fb0(fbsize), fb1(fbsize);
    std::vector<s16> s0(48000), s1(48000);
    int total_bad = 0;
    for (int f = 0; f < frames; f++)
    {
        for (size_t i = 0; i < evs.size(); i++)
            if (evs[i].frame == f) { apply(core[0], evs[i]); apply(core[1], evs[i]); }
        int n0 = 0, n1 = 0;
        core[0]->RunToVBlank(fb0.data(), s0.data(), &n0);
        core[1]->RunToVBlank(fb1.data(), s1.data(), &n1);
        int bad = compare(core[0], core[1], fb0.data(), fb1.data(), fbsize, s0.data(), s1.data(), n0, n1, f);
        if (bad)
        {
            total_bad += bad;
            if (total_bad > 20) { printf("too many differences, stopping\n"); break; }
        }
    }
    printf("lockstep done: %d frames, %d differences, translated blocks run %llu, misses %llu\n",
        frames, total_bad, RondoXlat::s_blocks_run, RondoXlat::s_misses);
    return total_bad ? 2 : 0;
}
