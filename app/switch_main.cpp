// rondo-nx for Nintendo Switch (libnx homebrew, .nro).
// Same core and translated code as the PC build; video through the libnx
// framebuffer, sound through audout (resampled 44.1 kHz -> 48 kHz), input
// through the pad API.
//
// SD card layout:
//   sdmc:/switch/rondo-nx/rondo-nx.nro
//   sdmc:/switch/rondo-nx/syscard3.pce
//   sdmc:/switch/rondo-nx/game/<English (fan translation v1.03) .cue and all .bin tracks>
//   sdmc:/switch/rondo-nx/game-jp/<optional: Japanese original .cue and .bin tracks>
//   sdmc:/switch/rondo-nx/rondo-nx.ini   (optional: language=japanese)
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
#include "cdrom_media.h"
#include "cdrom_cuebin_image.h"
#include "rondo_xlat_rt.h"
#include "rondo_wide.h"
#include "huc6270.h"
#include "menu.h"
#include <time.h>

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
static int s_volume = 10;                           // 0..10

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
        s_pending.push_back((s16)((l0 + (l1 - l0) * t) * s_volume / 10));
        s_pending.push_back((s16)((r0 + (r1 - r0) * t) * s_volume / 10));
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

struct Rect { int x, y, w, h; };

static Rect fit_rect(float aspect, bool stretch, int out_w, int out_h)
{
    if (stretch) { Rect r = {0, 0, out_w, out_h}; return r; }
    int dh = out_h;
    int dw = (int)(dh * aspect + 0.5f);
    if (dw > out_w) { dw = out_w; dh = (int)(dw / aspect + 0.5f); }
    Rect r = {(out_w - dw) / 2, (out_h - dh) / 2, dw, dh};
    return r;
}

static void blit(u32* dst, u32 stride_px, const u32* src, int sw, int sh, Rect r, int out_w, int out_h)
{
    static std::vector<int> xmap;
    xmap.resize(r.w);
    for (int x = 0; x < r.w; x++) xmap[x] = x * sw / r.w;
    for (int y = 0; y < out_h; y++)
    {
        u32* row = dst + (size_t)y * stride_px;
        if (y < r.y || y >= r.y + r.h) { memset(row, 0, out_w * 4); continue; }
        int sy = (y - r.y) * sh / r.h;
        const u32* srow = src + (size_t)sy * sw;
        if (y > r.y && ((y - 1 - r.y) * sh / r.h) == sy)
        {
            memcpy(row, row - stride_px, out_w * 4);  // same source row as the line above
            continue;
        }
        memset(row, 0, r.x * 4);
        for (int x = 0; x < r.w; x++) row[r.x + x] = srow[xmap[x]];
        memset(row + r.x + r.w, 0, (out_w - r.x - r.w) * 4);
    }
}

// Lays the menu overlay (RGBA with alpha, Menu::W x Menu::H) over the picture.
static void blend_overlay(u32* dst, u32 stride_px, const uint8_t* ov, Rect r)
{
    for (int y = 0; y < r.h; y++)
    {
        const uint8_t* srow = ov + (size_t)(y * Menu::H / r.h) * Menu::W * 4;
        uint8_t* drow = (uint8_t*)(dst + (size_t)(r.y + y) * stride_px + r.x);
        for (int x = 0; x < r.w; x++)
        {
            const uint8_t* s = srow + (x * Menu::W / r.w) * 4;
            unsigned a = s[3];
            if (!a) continue;
            uint8_t* d = drow + x * 4;
            d[0] = (uint8_t)((s[0] * a + d[0] * (255 - a)) / 255);
            d[1] = (uint8_t)((s[1] * a + d[1] * (255 - a)) / 255);
            d[2] = (uint8_t)((s[2] * a + d[2] * (255 - a)) / 255);
        }
    }
}


// ---------------------------------------------------------------- presenting on a second core
//
// Scaling the picture to 1280x720 and waiting for the display both happen on
// core 1, so the emulation thread (core 0) gets the whole frame time for the
// game itself and can start the next frame while this one is being drawn.

struct PresentQueue
{
    Mutex lock;
    CondVar cv;
    bool has = false;
    bool quit = false;
    std::vector<u32> pic;
    int w = 0, h = 0;
    Rect r = {0, 0, 1280, 720};
    Rect r_ov = {0, 0, 1280, 720};      // where the menu overlay goes (the 4:3 middle in widescreen)
    bool overlay_on = false;
    std::vector<uint8_t> overlay;
    bool wide = false;                  // build the widescreen picture from snap on the render thread
    RondoWide::Snapshot* snap = NULL;
};
static PresentQueue s_pq;
static Framebuffer s_fb;

static void render_thread(void*)
{
    std::vector<u32> pic;
    std::vector<uint8_t> ov(Menu::W * Menu::H * 4);
    std::vector<u32> widepic;
    RondoWide::Snapshot* snap = new RondoWide::Snapshot();
    for (;;)
    {
        mutexLock(&s_pq.lock);
        while (!s_pq.has && !s_pq.quit) condvarWait(&s_pq.cv, &s_pq.lock);
        if (s_pq.quit) { mutexUnlock(&s_pq.lock); break; }
        pic.swap(s_pq.pic);
        int w = s_pq.w, h = s_pq.h;
        Rect r = s_pq.r, r_ov = s_pq.r_ov;
        bool ov_on = s_pq.overlay_on;
        if (ov_on) ov.swap(s_pq.overlay);
        bool wide = s_pq.wide;
        if (wide) { RondoWide::Snapshot* t = s_pq.snap; s_pq.snap = snap; snap = t; }
        s_pq.has = false;
        condvarWakeAll(&s_pq.cv);
        mutexUnlock(&s_pq.lock);

        const u32* shown = pic.data();
        int shown_w = w;
        if (wide)
        {
            shown_w = w + 2 * RondoWide::kExtra;
            widepic.resize((size_t)shown_w * h);
            RondoWide::Compose(*snap, (const uint8_t*)pic.data(), w, h, (uint8_t*)widepic.data(), 0);
            shown = widepic.data();
        }
        u32 stride;
        u32* px = (u32*)framebufferBegin(&s_fb, &stride);
        blit(px, stride / 4, shown, shown_w, h, r, 1280, 720);
        if (ov_on) blend_overlay(px, stride / 4, ov.data(), r_ov);
        framebufferEnd(&s_fb);
    }
}

// Hands a finished frame to the render thread. Waits only if the previous one
// hasn't been picked up yet (which is what paces emulation to the display).
static void present(const u8* frame, int w, int h, Rect r, Rect r_ov, const uint8_t* overlay,
                    GeargrafxCore* wide_core = NULL, bool side_ok = false)
{
    mutexLock(&s_pq.lock);
    while (s_pq.has && !s_pq.quit) condvarWait(&s_pq.cv, &s_pq.lock);
    s_pq.wide = wide_core != NULL;
    if (wide_core)
    {
        if (!s_pq.snap) s_pq.snap = new RondoWide::Snapshot();
        RondoWide::Capture(wide_core, s_pq.snap, side_ok);
    }
    s_pq.pic.resize((size_t)w * h);
    memcpy(s_pq.pic.data(), frame, (size_t)w * h * 4);
    s_pq.w = w; s_pq.h = h; s_pq.r = r; s_pq.r_ov = r_ov;
    s_pq.overlay_on = overlay != NULL;
    if (overlay)
    {
        s_pq.overlay.resize(Menu::W * Menu::H * 4);
        memcpy(s_pq.overlay.data(), overlay, Menu::W * Menu::H * 4);
    }
    s_pq.has = true;
    condvarWakeAll(&s_pq.cv);
    mutexUnlock(&s_pq.lock);
}

// ---------------------------------------------------------------- disc caching
//
// Reads the whole disc into memory in the background (core 2, low priority) so
// that music streaming and stage loads never wait on the SD card mid-game.

static Thread s_cache_thread;
static bool s_cache_running = false;
static volatile bool s_cache_stop = false;
static volatile bool s_cache_done = false;
static CdRomCueBinImage* s_cache_img = NULL;

static void cache_thread(void*)
{
    while (!s_cache_stop && s_cache_img->BackgroundPreloadStep()) {}
    if (!s_cache_stop) s_cache_done = true;
}

static bool cache_enabled()
{
    // Needs room for the whole disc (about 550 MB). A title override launch has
    // plenty; the album applet doesn't, so it streams from the SD card instead.
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    return total > used && total - used > (u64)1200 * 1024 * 1024;
}

static void cache_start(GeargrafxCore* core)
{
    s_cache_done = false;
    if (!cache_enabled()) return;
    s_cache_img = core->GetCDROMMedia()->GetCueBinImage();
    s_cache_stop = false;
    if (R_FAILED(threadCreate(&s_cache_thread, cache_thread, NULL, NULL, 0x10000, 0x3B, 2))) return;
    threadStart(&s_cache_thread);
    s_cache_running = true;
}

static void cache_stop()
{
    if (!s_cache_running) return;
    s_cache_stop = true;
    threadWaitForExit(&s_cache_thread);
    threadClose(&s_cache_thread);
    s_cache_running = false;
}


// ---------------------------------------------------------------- save states
//
// A state is copied to memory in one go (well under a frame), then written to
// the SD card by a background thread in a single large write, so the game never
// stalls on storage. Loading reads the whole file with one read.

static Thread s_state_thread;
static bool s_state_busy = false;           // a write thread exists (maybe finished)
static volatile bool s_state_done = false;
static volatile bool s_state_ok = false;
static int s_state_slot = 0;
static std::vector<u8> s_state_buf;
static size_t s_state_size = 0;
static std::string s_state_target;

static bool write_file_whole(const std::string& path, const u8* data, size_t size)
{
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

static void state_write_thread(void*)
{
    s_state_ok = write_file_whole(s_state_target, s_state_buf.data(), s_state_size);
    s_state_done = true;
}

// Waits for a pending state write (if any) to finish.
static void state_wait()
{
    if (!s_state_busy) return;
    threadWaitForExit(&s_state_thread);
    threadClose(&s_state_thread);
    s_state_busy = false;
}

static bool state_save_async(GeargrafxCore* core, const std::string& path, int slot)
{
    state_wait();
    s_state_buf.resize(4 * 1024 * 1024);
    size_t size = s_state_buf.size();
    if (!core->SaveState(s_state_buf.data(), size, false)) return false;
    s_state_size = size;
    s_state_target = path;
    s_state_slot = slot;
    s_state_done = false;
    if (R_FAILED(threadCreate(&s_state_thread, state_write_thread, NULL, NULL, 0x10000, 0x2C, 2)))
    {
        s_state_ok = write_file_whole(path, s_state_buf.data(), size);
        s_state_done = true;
        return true;
    }
    threadStart(&s_state_thread);
    s_state_busy = true;
    return true;
}

static bool state_load(GeargrafxCore* core, const std::string& path)
{
    state_wait();
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    std::vector<u8> buf((size_t)n);
    bool ok = fread(buf.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok && core->LoadState(buf.data(), (size_t)n);
}

// ---------------------------------------------------------------- menu host (Switch)

static std::string s_saves;
static bool s_japanese = false;

static std::string state_path(int slot)
{
    char name[48];
    snprintf(name, sizeof(name), "/%s.state%d", s_japanese ? "rondo" : "rondo-en", slot);
    return s_saves + name;
}

// Bindings use the bit index of the HidNpadButton.
static const char* npad_name(int bit)
{
    static const char* names[] = {"A", "B", "X", "Y", "L3", "R3", "L", "R", "ZL", "ZR", "+", "-",
                                  "D-Left", "D-Up", "D-Right", "D-Down"};
    return (bit >= 0 && bit < 16) ? names[bit] : "?";
}

// Save state slot times, read once (at start and after a language switch) and updated after each
// save. The menu shows them every frame; asking the SD card each frame would wait behind a save
// state that is still being written in the background, freezing the menu until it finishes.
static std::string s_slot_info[6];
static void slot_info_set_time(int slot, time_t t)
{
    struct tm* lt = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%m/%d %H:%M", lt);
    s_slot_info[slot] = buf;
}
static void slot_info_refresh()
{
    for (int slot = 1; slot <= 5; slot++)
    {
        struct stat st;
        if (stat(state_path(slot).c_str(), &st) != 0) s_slot_info[slot] = "Empty";
        else slot_info_set_time(slot, st.st_mtime);
    }
}

struct SwitchHost : public MenuHost
{
    bool HasKeyboard() { return false; }
    bool HasWindow() { return false; }
    std::string KeyName(int) { return "-"; }
    std::string PadName(int code) { return npad_name(code); }
    std::string SlotInfo(int slot)
    {
        if (slot < 1 || slot > 5) return "Empty";
        return s_slot_info[slot];
    }
    void DefaultBindings(MenuSettings& s)
    {
        s.pad[BIND_UP] = 13;        // D-Up
        s.pad[BIND_DOWN] = 15;      // D-Down
        s.pad[BIND_LEFT] = 12;      // D-Left
        s.pad[BIND_RIGHT] = 14;     // D-Right
        s.pad[BIND_ATTACK] = 3;     // Y
        s.pad[BIND_JUMP] = 1;       // B
        s.pad[BIND_SUBWEAPON] = 2;  // X
        s.pad[BIND_RUN] = 10;       // +
        s.pad[BIND_SELECT] = 11;    // -
    }
};

static std::string read_text(const std::string& path)
{
    std::string out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

static void write_text(const std::string& path, const std::string& text)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    std::string ini = std::string(k_dir) + "/rondo-nx.ini";
    std::string cue_override, bios = std::string(k_dir) + "/syscard3.pce";
    bool xlat = true;
    int ini_language = 0;
    std::string ini_text = read_text(ini);
    {
        size_t pos = 0;
        while (pos < ini_text.size())
        {
            size_t end = ini_text.find('\n', pos);
            if (end == std::string::npos) end = ini_text.size();
            std::string l = ini_text.substr(pos, end - pos);
            pos = end + 1;
            while (!l.empty() && (l[l.size() - 1] == '\r' || l[l.size() - 1] == ' ')) l.erase(l.size() - 1);
            if (l.compare(0, 4, "cue=") == 0) cue_override = l.substr(4);
            else if (l.compare(0, 5, "bios=") == 0) bios = l.substr(5);
            else if (l == "translated=0") xlat = false;
            else if (l == "language=japanese") ini_language = 1;
        }
    }

    s_saves = std::string(k_dir) + "/saves";
    mkdir(s_saves.c_str(), 0777);
    std::string settings_path = s_saves + "/settings.ini";

    SwitchHost host;
    MenuSettings ms;
    host.DefaultBindings(ms);
    ms.language = ini_language;
    std::string saved = read_text(settings_path);
    if (!saved.empty()) ms.Parse(saved);
    Menu menu(&host, &ms);
    s_volume = ms.volume;

    std::string cue_for[2] = { find_cue(std::string(k_dir) + "/game"), find_cue(std::string(k_dir) + "/game-jp") };
    if (!cue_override.empty()) cue_for[ms.language] = cue_override;
    if (cue_for[ms.language].empty() && !cue_for[1 - ms.language].empty()) ms.language = 1 - ms.language;
    s_japanese = (ms.language == 1);
    slot_info_refresh();
    std::string cue = cue_for[ms.language];
    if (cue.empty() || !exists(cue))
    {
        show_error("Game not found. Put the English .cue and all .bin tracks in\n  sdmc:/switch/rondo-nx/game/");
        return 0;
    }
    if (!exists(bios)) { show_error("System Card not found. Copy syscard3.pce to\n  sdmc:/switch/rondo-nx/syscard3.pce"); return 0; }

    std::string bram = s_saves + (s_japanese ? "/rondo.sav" : "/rondo-en.sav");

    GeargrafxCore* core = new GeargrafxCore();
    core->Init(NULL, GG_PIXEL_RGBA8888);
    if (!core->LoadBios(bios.c_str(), true) || !core->LoadMedia(cue.c_str()))
    {
        show_error("The disc image or System Card failed to load.");
        return 0;
    }
    BackupSaveFile(bram);
    if (exists(bram)) core->LoadRam(bram.c_str(), true);
    rondo_xlat_attach(core, xlat);

    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
    // Run the CPU at its boost clock (about 1.8 GHz instead of 1 GHz), the same mode
    // official games use while loading. Only the CPU does work here, so the lower
    // GPU clock that comes with it doesn't matter. Ignored when launched as an applet.
    appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);

    NWindow* win = nwindowGetDefault();
    framebufferCreate(&s_fb, win, 1280, 720, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&s_fb);
    mutexInit(&s_pq.lock);
    condvarInit(&s_pq.cv);
    Thread render;
    bool render_ok = R_SUCCEEDED(threadCreate(&render, render_thread, NULL, NULL, 0x10000, 0x2B, 1));
    if (render_ok) threadStart(&render);
    cache_start(core);

    audio_init();

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    std::vector<u8> frame(2048 * 512 * 4);
    std::vector<s16> snd(GG_AUDIO_BUFFER_SIZE * 4);
    std::vector<uint8_t> overlay(Menu::W * Menu::H * 4);
    std::vector<u8> wide;
    u16 sent = 0;
    int boot = 0;
    u64 perf_t0 = armGetSystemTick(), perf_sum = 0, perf_max = 0;
    u32 perf_frames = 0, perf_slow = 0, perf_reads = 0;
    u64 frames = 0;
    bool running = true;

    static const GG_Keys keys[] = {GG_KEY_I, GG_KEY_II, GG_KEY_SELECT, GG_KEY_RUN, GG_KEY_UP, GG_KEY_RIGHT, GG_KEY_DOWN, GG_KEY_LEFT};
    auto send = [&](u16 want)
    {
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        {
            u16 k = (u16)keys[i];
            if ((want & k) && !(sent & k)) core->KeyPressed(GG_CONTROLLER_1, keys[i]);
            if (!(want & k) && (sent & k)) core->KeyReleased(GG_CONTROLLER_1, keys[i]);
        }
        sent = want;
    };
    auto save_settings = [&]() { write_text(settings_path, ms.Serialize()); };
    int give_sub = -1;   // subweapon waiting to be handed over (applied during gameplay)

    auto handle = [&](MenuResult r)
    {
        switch (r)
        {
        case MR_RESET: core->ResetMedia(true); boot = 0; menu.Toast("Reset"); break;
        case MR_SAVE_STATE:
        {
            char t[40];
            if (!state_save_async(core, state_path(ms.slot), ms.slot))
            {
                snprintf(t, sizeof(t), "Couldn't save slot %d", ms.slot);
                menu.Toast(t);
            }
            else
                menu.Toast("Saving...");
            break;
        }
        case MR_LOAD_STATE:
        {
            char t[40];
            bool ok = state_load(core, state_path(ms.slot));
            snprintf(t, sizeof(t), ok ? "Loaded slot %d" : "Couldn't load slot %d", ms.slot);
            menu.Toast(t);
            break;
        }
        case MR_LANGUAGE:
            if (cue_for[ms.language].empty())
            {
                ms.language = 1 - ms.language;
                menu.Toast(ms.language ? "No English disc in game/" : "No Japanese disc in game-jp/");
                break;
            }
            core->SaveRam(bram.c_str(), true);
            s_japanese = (ms.language == 1);
            bram = s_saves + (s_japanese ? "/rondo.sav" : "/rondo-en.sav");
            cache_stop();
            if (core->LoadMedia(cue_for[ms.language].c_str()))
            {
                BackupSaveFile(bram);
                if (exists(bram)) core->LoadRam(bram.c_str(), true);
                core->ResetMedia(true);
                boot = 0;
                menu.Toast(s_japanese ? "Japanese" : "English");
            }
            cache_start(core);
            slot_info_refresh();
            break;
        case MR_VOLUME: s_volume = ms.volume; break;
        case MR_SUBWEAPON:
            // Swap it in right away when a stage is loaded (the game is paused behind the menu, so
            // "Holding now" shows it at once); otherwise it's handed over when the next stage starts.
            give_sub = ms.subweapon;
            if (ApplyCheats(core->GetMemory()->GetMemoryMap()[0xF8], ms, give_sub))
            {
                menu.Toast(std::string("Subweapon: ") + SubweaponName(give_sub));
                give_sub = -1;
            }
            menu.SetCurrentSubweapon(core->GetMemory()->GetMemoryMap()[0xF8][0x99] ? core->GetMemory()->GetMemoryMap()[0xF8][0x93] : -1);
            break;
        case MR_QUIT: running = false; break;
        default: break;
        }
        if (r != MR_NONE) save_settings();
    };

    while (running && appletMainLoop())
    {
        padUpdate(&pad);
        u64 b = padGetButtons(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (menu.IsOpen())
        {
            if (menu.IsCapturing())
            {
                for (int bit = 0; bit < 16; bit++)
                    if (down & BITL(bit)) { handle(menu.Captured(bit)); break; }
            }
            else
            {
                if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) handle(menu.Nav(NAV_UP));
                if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) handle(menu.Nav(NAV_DOWN));
                if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) handle(menu.Nav(NAV_LEFT));
                if (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) handle(menu.Nav(NAV_RIGHT));
                if (down & HidNpadButton_A) handle(menu.Nav(NAV_OK));
                if (down & HidNpadButton_B) handle(menu.Nav(NAV_BACK));
                if (down & HidNpadButton_StickR) handle(menu.Close());
            }
        }
        else
        {
            // Open the menu: ZL + ZR together, or click the right stick.
            bool zlzr = (b & HidNpadButton_ZL) && (b & HidNpadButton_ZR) && (down & (HidNpadButton_ZL | HidNpadButton_ZR));
            if (zlzr || (down & HidNpadButton_StickR))
            {
                menu.Open();
                send(0);
            }
        }

        if (!menu.IsOpen())
        {
            u16 want = 0;
            static const u16 action_key[BIND_COUNT] = {GG_KEY_UP, GG_KEY_DOWN, GG_KEY_LEFT, GG_KEY_RIGHT,
                                                       GG_KEY_II, GG_KEY_I, (u16)(GG_KEY_UP | GG_KEY_II), GG_KEY_RUN, GG_KEY_SELECT};
            for (int a = 0; a < BIND_COUNT; a++)
                if (ms.pad[a] >= 0 && (b & BITL(ms.pad[a]))) want |= action_key[a];
            if (b & HidNpadButton_StickLUp) want |= GG_KEY_UP;
            if (b & HidNpadButton_StickLDown) want |= GG_KEY_DOWN;
            if (b & HidNpadButton_StickLLeft) want |= GG_KEY_LEFT;
            if (b & HidNpadButton_StickLRight) want |= GG_KEY_RIGHT;
            if (boot < 200) { boot++; if (boot >= 90 && boot < 96) want |= GG_KEY_RUN; }
            send(want);

            // Widescreen: the game keeps sprite pieces that reach into the side areas, and the
            // chip's 16 sprites per line limit is lifted (those extra pieces would count against it).
            RondoXlat::s_wide_sprites = (ms.aspect == 2);
            core->GetHuC6270_1()->SetNoSpriteLimit(ms.aspect == 2);
            int count = 0;
            u64 t0 = armGetSystemTick();
            core->RunToVBlank(frame.data(), snd.data(), &count);
            u64 dt = armTicksToNs(armGetSystemTick() - t0);
            perf_sum += dt;
            if (dt > perf_max) perf_max = dt;
            if (dt > 16600000ULL) perf_slow++;
            perf_frames++;
            audio_push(snd.data(), count);
            if (ApplyCheats(core->GetMemory()->GetMemoryMap()[0xF8], ms, give_sub))
            {
                menu.Toast(std::string("Subweapon: ") + SubweaponName(give_sub));
                give_sub = -1;
            }
        }
        menu.Tick();
        {
            const u8* w = core->GetMemory()->GetMemoryMap()[0xF8];
            menu.SetCurrentSubweapon(w[0x99] ? w[0x93] : -1);
        }

        // Report a finished background state write.
        if (s_state_done)
        {
            s_state_done = false;
            char t[40];
            snprintf(t, sizeof(t), s_state_ok ? "Saved to slot %d" : "Couldn't save slot %d", s_state_slot);
            menu.Toast(t);
            if (s_state_ok && s_state_slot >= 1 && s_state_slot <= 5) slot_info_set_time(s_state_slot, time(NULL));
        }

        // Performance readout, refreshed once a second.
        u64 now = armGetSystemTick();
        if (armTicksToNs(now - perf_t0) >= 1000000000ULL)
        {
            double secs = armTicksToNs(now - perf_t0) / 1e9;
            u32 reads = CdRomCueBinImage::s_sync_loads.load();
            char hud[48];
            snprintf(hud, sizeof(hud), "%2.0ffps %4.1f/%4.1fms slow%2u rd%2u %s",
                perf_frames / secs, perf_frames ? perf_sum / 1e6 / perf_frames : 0.0, perf_max / 1e6,
                perf_slow, reads - perf_reads, s_cache_done ? "RAM" : (s_cache_running ? "..." : "SD"));
            menu.SetHud(hud);
            perf_t0 = now; perf_sum = 0; perf_max = 0; perf_frames = 0; perf_slow = 0; perf_reads = reads;
        }

        GG_Runtime_Info ri;
        core->GetRuntimeInfo(ri);
        float aspect = ri.aspect_ratio > 0 ? ri.aspect_ratio : 4.0f / 3.0f;
        const u8* pic = frame.data();
        int pic_w = ri.screen_width;
        Rect r, r_ov;
        const bool widescreen = ms.aspect == 2;
        const bool side_ok = core->GetMemory()->GetMemoryMap()[0xF8][0x99] != 0;   // gameplay
        if (widescreen)
        {
            // Widescreen: extra scenery drawn at both sides during gameplay, black bars elsewhere.
            // The render thread builds the picture from a snapshot; without it, do it here.
            pic_w = ri.screen_width + 2 * RondoWide::kExtra;
            if (!render_ok)
            {
                wide.resize((size_t)pic_w * ri.screen_height * 4);
                RondoWide::Compose(core, frame.data(), ri.screen_width, ri.screen_height, wide.data(), side_ok, 0);
                pic = wide.data();
            }
            r = fit_rect(aspect * pic_w / ri.screen_width, false, 1280, 720);
            r_ov = r;
            r_ov.w = r.w * ri.screen_width / pic_w;
            r_ov.x = r.x + (r.w - r_ov.w) / 2;
        }
        else
            r_ov = r = fit_rect(aspect, ms.aspect == 1, 1280, 720);
        bool ov = menu.Visible();
        if (ov) menu.Draw(overlay.data());
        if (render_ok)
            present(frame.data(), ri.screen_width, ri.screen_height, r, r_ov, ov ? overlay.data() : NULL,
                    widescreen ? core : NULL, side_ok);
        else
        {
            u32 stride;
            u32* px = (u32*)framebufferBegin(&s_fb, &stride);
            blit(px, stride / 4, (const u32*)pic, pic_w, ri.screen_height, r, 1280, 720);
            if (ov) blend_overlay(px, stride / 4, overlay.data(), r_ov);
            framebufferEnd(&s_fb);
        }

        if (++frames % 1800 == 0)
            core->SaveRam(bram.c_str(), true);
    }

    core->SaveRam(bram.c_str(), true);
    save_settings();
    state_wait();
    cache_stop();
    if (render_ok)
    {
        mutexLock(&s_pq.lock);
        s_pq.quit = true;
        condvarWakeAll(&s_pq.cv);
        mutexUnlock(&s_pq.lock);
        threadWaitForExit(&render);
        threadClose(&render);
    }
    appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    framebufferClose(&s_fb);
    if (s_audio_ok) { audoutStopAudioOut(); audoutExit(); }
    return 0;
}
