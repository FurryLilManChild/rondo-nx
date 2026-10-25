// rondo-nx for Nintendo Switch (libnx homebrew, .nro).
// Same core and translated code as the PC build; video through the libnx
// framebuffer, sound through audout (resampled 44.1 kHz -> 48 kHz), input
// through the pad API.
//
// SD card layout:
//   sdmc:/switch/rondo-nx/rondo-nx.nro
//   sdmc:/switch/rondo-nx/syscard3.pce
//   sdmc:/switch/rondo-nx/game/<the .cue and all the .bin tracks>
//   sdmc:/switch/rondo-nx/saves/            (created on first run)
#include <switch.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include "geargrafx.h"
#include "rondo_xlat_rt.h"

bool g_mcp_stdio_mode = false;
void rondo_xlat_attach(GeargrafxCore* core, bool enabled);

static const char* k_dir = "sdmc:/switch/rondo-nx";

static bool exists(const std::string& p)
{
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static std::string find_cue(const std::string& dir)
{
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    while (struct dirent* e = readdir(d))
    {
        std::string n = e->d_name;
        if (n.size() > 4 && (n.substr(n.size() - 4) == ".cue" || n.substr(n.size() - 4) == ".CUE"))
        {
            found = dir + "/" + n;
            break;
        }
    }
    closedir(d);
    return found;
}

// Simple text screen for setup errors (before the game runs).
static void show_error(const char* msg)
{
    consoleInit(NULL);
    printf("\n  rondo-nx\n\n  %s\n\n  Press + to exit.\n", msg);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
}

// ---------------------------------------------------------------- audio

static const int k_out_rate = 48000;
static const int k_bufs = 4;
static const int k_buf_frames = 1024;              // stereo frames per buffer
static AudioOutBuffer s_aout[k_bufs];
static s16* s_amem[k_bufs];
static std::vector<s16> s_pending;                  // resampled, waiting to be queued
static double s_rs_pos = 0.0;
static s16 s_last_l = 0, s_last_r = 0;
static bool s_audio_ok = false;
static int s_free_bufs = k_bufs;
static bool s_buf_free[k_bufs];

static void audio_init()
{
    if (R_FAILED(audoutInitialize())) return;
    if (R_FAILED(audoutStartAudioOut())) { audoutExit(); return; }
    size_t size = (k_buf_frames * 2 * sizeof(s16) + 0xFFF) & ~0xFFF;
    for (int i = 0; i < k_bufs; i++)
    {
        s_amem[i] = (s16*)memalign(0x1000, size);
        memset(s_amem[i], 0, size);
        s_aout[i].next = NULL;
        s_aout[i].buffer = s_amem[i];
        s_aout[i].buffer_size = size;
        s_aout[i].data_size = k_buf_frames * 2 * sizeof(s16);
        s_aout[i].data_offset = 0;
        s_buf_free[i] = true;
    }
    s_audio_ok = true;
}

// Linear resample of interleaved stereo 44.1 kHz into s_pending at 48 kHz.
static void audio_push(const s16* in, int samples)
{
    if (!s_audio_ok) return;
    int frames = samples / 2;
    const double step = (double)GG_AUDIO_SAMPLE_RATE / (double)k_out_rate;
    while (s_rs_pos < frames)
    {
        int i = (int)s_rs_pos;
        double t = s_rs_pos - i;
        s16 l0 = i == 0 ? s_last_l : in[(i - 1) * 2], r0 = i == 0 ? s_last_r : in[(i - 1) * 2 + 1];
        s16 l1 = in[i * 2], r1 = in[i * 2 + 1];
        s_pending.push_back((s16)(l0 + (l1 - l0) * t));
        s_pending.push_back((s16)(r0 + (r1 - r0) * t));
        s_rs_pos += step;
    }
    s_rs_pos -= frames;
    if (frames > 0) { s_last_l = in[(frames - 1) * 2]; s_last_r = in[(frames - 1) * 2 + 1]; }

    // Hand finished buffers back.
    AudioOutBuffer* rel = NULL;
    u32 count = 0;
    while (R_SUCCEEDED(audoutGetReleasedAudioOutBuffer(&rel, &count)) && count > 0 && rel)
    {
        for (int i = 0; i < k_bufs; i++)
            if (&s_aout[i] == rel) { s_buf_free[i] = true; s_free_bufs++; }
        rel = NULL;
    }
    // Queue full buffers while we have them. Keep latency bounded.
    const size_t chunk = k_buf_frames * 2;
    while (s_pending.size() >= chunk)
    {
        int b = -1;
        for (int i = 0; i < k_bufs; i++) if (s_buf_free[i]) { b = i; break; }
        if (b < 0)
        {
            if (s_pending.size() > chunk * 3)
                s_pending.erase(s_pending.begin(), s_pending.begin() + chunk);  // drop to catch up
            break;
        }
        memcpy(s_amem[b], s_pending.data(), chunk * sizeof(s16));
        s_pending.erase(s_pending.begin(), s_pending.begin() + chunk);
        s_buf_free[b] = false;
        s_free_bufs--;
        audoutAppendAudioOutBuffer(&s_aout[b]);
    }
}

// ---------------------------------------------------------------- video

static void blit(u32* dst, u32 stride_px, const u32* src, int sw, int sh, float aspect, int out_w, int out_h)
{
    int dh = out_h;
    int dw = (int)(dh * aspect + 0.5f);
    if (dw > out_w) { dw = out_w; dh = (int)(dw / aspect + 0.5f); }
    int ox = (out_w - dw) / 2, oy = (out_h - dh) / 2;
    static std::vector<int> xmap;
    xmap.resize(dw);
    for (int x = 0; x < dw; x++) xmap[x] = x * sw / dw;
    for (int y = 0; y < out_h; y++)
    {
        u32* row = dst + (size_t)y * stride_px;
        if (y < oy || y >= oy + dh) { memset(row, 0, out_w * 4); continue; }
        int sy = (y - oy) * sh / dh;
        const u32* srow = src + (size_t)sy * sw;
        if (y > oy && ((y - 1 - oy) * sh / dh) == sy)
        {
            memcpy(row, row - stride_px, out_w * 4);  // same source row as the line above
            continue;
        }
        memset(row, 0, ox * 4);
        for (int x = 0; x < dw; x++) row[ox + x] = srow[xmap[x]];
        memset(row + ox + dw, 0, (out_w - ox - dw) * 4);
    }
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    std::string ini = std::string(k_dir) + "/rondo-nx.ini";
    std::string cue, bios = std::string(k_dir) + "/syscard3.pce";
    bool xlat = true;
    FILE* f = fopen(ini.c_str(), "r");
    if (f)
    {
        char line[1024];
        while (fgets(line, sizeof(line), f))
        {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (l.compare(0, 4, "cue=") == 0) cue = l.substr(4);
            else if (l.compare(0, 5, "bios=") == 0) bios = l.substr(5);
            else if (l == "translated=0") xlat = false;
        }
        fclose(f);
    }
    if (cue.empty()) cue = find_cue(std::string(k_dir) + "/game");
    if (cue.empty() || !exists(cue)) { show_error("Game not found. Put the .cue and all .bin tracks in\n  sdmc:/switch/rondo-nx/game/"); return 0; }
    if (!exists(bios)) { show_error("System Card not found. Copy syscard3.pce to\n  sdmc:/switch/rondo-nx/syscard3.pce"); return 0; }

    std::string saves = std::string(k_dir) + "/saves";
    mkdir(saves.c_str(), 0777);
    std::string bram = saves + "/rondo.sav";

    GeargrafxCore* core = new GeargrafxCore();
    core->Init(NULL, GG_PIXEL_RGBA8888);
    if (!core->LoadBios(bios.c_str(), true) || !core->LoadMedia(cue.c_str()))
    {
        show_error("The disc image or System Card failed to load.");
        return 0;
    }
    if (exists(bram)) core->LoadRam(bram.c_str(), true);
    rondo_xlat_attach(core, xlat);

    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
    // Run the CPU at its boost clock (about 1.8 GHz instead of 1 GHz), the same mode
    // official games use while loading. Only the CPU does work here, so the lower
    // GPU clock that comes with it doesn't matter. Ignored when launched as an applet.
    appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);

    Framebuffer fb;
    NWindow* win = nwindowGetDefault();
    framebufferCreate(&fb, win, 1280, 720, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&fb);

    audio_init();

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    std::vector<u8> frame(2048 * 512 * 4);
    std::vector<s16> snd(GG_AUDIO_BUFFER_SIZE * 4);
    u16 sent = 0;
    int boot = 0;
    u64 frames = 0;

    while (appletMainLoop())
    {
        padUpdate(&pad);
        u64 b = padGetButtons(&pad);
        if ((b & HidNpadButton_Plus) && (b & HidNpadButton_Minus))
            break;  // hold + and - together to quit

        u16 want = 0;
        if (b & HidNpadButton_AnyUp) want |= GG_KEY_UP;
        if (b & HidNpadButton_AnyDown) want |= GG_KEY_DOWN;
        if (b & HidNpadButton_AnyLeft) want |= GG_KEY_LEFT;
        if (b & HidNpadButton_AnyRight) want |= GG_KEY_RIGHT;
        if (b & HidNpadButton_Y) want |= GG_KEY_I;             // attack
        if (b & HidNpadButton_B) want |= GG_KEY_II;            // jump
        if (b & HidNpadButton_X) want |= GG_KEY_UP | GG_KEY_I; // subweapon
        if (b & HidNpadButton_Plus) want |= GG_KEY_RUN;
        if (b & HidNpadButton_Minus) want |= GG_KEY_SELECT;
        if (boot < 200) { boot++; if (boot >= 90 && boot < 96) want |= GG_KEY_RUN; }

        static const GG_Keys keys[] = {GG_KEY_I, GG_KEY_II, GG_KEY_SELECT, GG_KEY_RUN, GG_KEY_UP, GG_KEY_RIGHT, GG_KEY_DOWN, GG_KEY_LEFT};
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        {
            u16 k = (u16)keys[i];
            if ((want & k) && !(sent & k)) core->KeyPressed(GG_CONTROLLER_1, keys[i]);
            if (!(want & k) && (sent & k)) core->KeyReleased(GG_CONTROLLER_1, keys[i]);
        }
        sent = want;

        int count = 0;
        core->RunToVBlank(frame.data(), snd.data(), &count);
        audio_push(snd.data(), count);

        GG_Runtime_Info ri;
        core->GetRuntimeInfo(ri);
        u32 stride;
        u32* px = (u32*)framebufferBegin(&fb, &stride);
        blit(px, stride / 4, (const u32*)frame.data(), ri.screen_width, ri.screen_height,
             ri.aspect_ratio > 0 ? ri.aspect_ratio : 4.0f / 3.0f, 1280, 720);
        framebufferEnd(&fb);

        if (++frames % 1800 == 0)
            core->SaveRam(bram.c_str(), true);
    }

    core->SaveRam(bram.c_str(), true);
    appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    framebufferClose(&fb);
    if (s_audio_ok) { audoutStopAudioOut(); audoutExit(); }
    return 0;
}
