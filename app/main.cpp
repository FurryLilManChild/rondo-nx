// rondo-nx: standalone runner for Castlevania: Rondo of Blood (PC Engine CD).
// Game code runs as translated C++ (generated from coverage traces); anything
// not translated falls back to the HuC6280 interpreter. Video, sound and CD
// hardware come from the Geargrafx core (GPL-3.0) for this first milestone.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "geargrafx.h"
#include "rondo_xlat_rt.h"

bool g_mcp_stdio_mode = false;
void rondo_xlat_attach(GeargrafxCore* core, bool enabled);

static std::string g_base;

static std::string join(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    char last = a[a.size() - 1];
    return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}

static bool file_exists(const std::string& p)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n\"");
    size_t b = s.find_last_not_of(" \t\r\n\"");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

struct Config
{
    std::string cue, cue_english, cue_japanese, bios, saves;
    std::string language = "english";
    bool xlat = true;
    int scale = 4;
    bool fullscreen = false;
    bool smooth = false;
};

static void load_config(Config& c)
{
    std::string path = join(g_base, "rondo-nx.ini");
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f))
    {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
        if (k == "cue") c.cue = v;
        else if (k == "cue_english") c.cue_english = v;
        else if (k == "cue_japanese") c.cue_japanese = v;
        else if (k == "language") c.language = v;
        else if (k == "bios") c.bios = v;
        else if (k == "saves") c.saves = v;
        else if (k == "translated") c.xlat = (v != "0" && v != "false");
        else if (k == "scale") c.scale = SDL_max(1, atoi(v.c_str()));
        else if (k == "fullscreen") c.fullscreen = (v == "1" || v == "true");
        else if (k == "smooth") c.smooth = (v == "1" || v == "true");
    }
    fclose(f);
}

struct Pad
{
    u16 state = 0;
};

static GeargrafxCore* g_core;
static Pad g_kb, g_pad;
static u16 g_sent = 0;

static void sync_input()
{
    u16 want = g_kb.state | g_pad.state;
    static const GG_Keys keys[] = {GG_KEY_I, GG_KEY_II, GG_KEY_SELECT, GG_KEY_RUN, GG_KEY_UP, GG_KEY_RIGHT, GG_KEY_DOWN, GG_KEY_LEFT};
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
        u16 k = (u16)keys[i];
        if ((want & k) && !(g_sent & k)) g_core->KeyPressed(GG_CONTROLLER_1, keys[i]);
        if (!(want & k) && (g_sent & k)) g_core->KeyReleased(GG_CONTROLLER_1, keys[i]);
    }
    g_sent = want;
}

static u16 key_for_scancode(SDL_Scancode sc)
{
    switch (sc)
    {
    case SDL_SCANCODE_UP: return GG_KEY_UP;
    case SDL_SCANCODE_DOWN: return GG_KEY_DOWN;
    case SDL_SCANCODE_LEFT: return GG_KEY_LEFT;
    case SDL_SCANCODE_RIGHT: return GG_KEY_RIGHT;
    case SDL_SCANCODE_X: return GG_KEY_I;          // attack
    case SDL_SCANCODE_Z: return GG_KEY_II;         // jump
    case SDL_SCANCODE_RETURN: return GG_KEY_RUN;
    case SDL_SCANCODE_RSHIFT:
    case SDL_SCANCODE_BACKSPACE: return GG_KEY_SELECT;
    default: return 0;
    }
}

static u16 g_pad_buttons = 0, g_pad_axes = 0, g_pad_macro = 0;

static void update_pad_state()
{
    g_pad.state = g_pad_buttons | g_pad_axes | g_pad_macro;
}

static u16 key_for_button(int b)
{
    switch (b)
    {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return GG_KEY_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return GG_KEY_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return GG_KEY_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return GG_KEY_RIGHT;
    case SDL_GAMEPAD_BUTTON_WEST: return GG_KEY_I;    // attack (Xbox X / Switch Y)
    case SDL_GAMEPAD_BUTTON_SOUTH: return GG_KEY_II;  // jump (Xbox A / Switch B)
    case SDL_GAMEPAD_BUTTON_START: return GG_KEY_RUN;
    case SDL_GAMEPAD_BUTTON_BACK: return GG_KEY_SELECT;
    default: return 0;
    }
}

int main(int argc, char** argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "rondo-nx", SDL_GetError(), NULL);
        return 1;
    }
    const char* bp = SDL_GetBasePath();
    g_base = bp ? bp : "./";

    Config cfg;
    load_config(cfg);
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--interpreted")) cfg.xlat = false;
        else if (!strcmp(argv[i], "--fullscreen")) cfg.fullscreen = true;
        else if (!strcmp(argv[i], "--japanese")) cfg.language = "japanese";
        else if (!strcmp(argv[i], "--english")) cfg.language = "english";
    }
    if (cfg.saves.empty()) cfg.saves = join(g_base, "saves");
    SDL_CreateDirectory(cfg.saves.c_str());

    // Pick the disc for the chosen language (English is the fan translation v1.03).
    bool japanese = (cfg.language == "japanese" || cfg.language == "jp");
    if (japanese && !cfg.cue_japanese.empty()) cfg.cue = cfg.cue_japanese;
    else if (!japanese && !cfg.cue_english.empty()) cfg.cue = cfg.cue_english;
    if (cfg.cue.empty() || !file_exists(cfg.cue) || cfg.bios.empty() || !file_exists(cfg.bios))
    {
        std::string msg = "Couldn't find the game files.\n\nEdit rondo-nx.ini next to rondo-nx.exe and set:\n"
                          "cue_english=<path to the English .cue file>\ncue_japanese=<path to the Japanese .cue file>\nbios=<path to syscard3.pce>\n\nCurrently (" + cfg.language + "):\ncue=" + cfg.cue + "\nbios=" + cfg.bios;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "rondo-nx", msg.c_str(), NULL);
        return 1;
    }

    g_core = new GeargrafxCore();
    g_core->Init(NULL, GG_PIXEL_RGBA8888);
    if (!g_core->LoadBios(cfg.bios.c_str(), true) || !g_core->LoadMedia(cfg.cue.c_str()))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "rondo-nx", "The emulated CD or System Card failed to load. Check the paths in rondo-nx.ini.", NULL);
        return 1;
    }
    // Separate saves per language: the two discs keep different save data.
    std::string bram = join(cfg.saves, japanese ? "rondo.sav" : "rondo-en.sav");
    std::string state = join(cfg.saves, japanese ? "rondo.state" : "rondo-en.state");
    if (file_exists(bram)) g_core->LoadRam(bram.c_str(), true);
    rondo_xlat_attach(g_core, cfg.xlat);

    SDL_Window* win = SDL_CreateWindow("rondo-nx", 320 * cfg.scale, 240 * cfg.scale, SDL_WINDOW_RESIZABLE | (cfg.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    SDL_Renderer* ren = SDL_CreateRenderer(win, NULL);
    SDL_SetRenderVSync(ren, 0);
    SDL_Texture* tex = NULL;
    int tex_w = 0, tex_h = 0;

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = GG_AUDIO_SAMPLE_RATE;
    SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio) SDL_ResumeAudioStreamDevice(audio);

    std::vector<u8> fb(2048 * 512 * 4);
    std::vector<s16> snd(GG_AUDIO_BUFFER_SIZE * 4);

    const double frame_ns = 1e9 / 59.826;
    Uint64 next = SDL_GetTicksNS();
    Uint64 last_title = 0, last_bram = SDL_GetTicks();
    int frames = 0;
    bool running = true, fast = false, paused = false;
    unsigned long long prev_x = 0, prev_m = 0;

    while (running)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                bool down = (e.type == SDL_EVENT_KEY_DOWN);
                SDL_Scancode sc = e.key.scancode;
                u16 k = key_for_scancode(sc);
                if (k) { if (down) g_kb.state |= k; else g_kb.state &= ~k; }
                if (sc == SDL_SCANCODE_TAB) fast = down;
                if (down && !e.key.repeat)
                {
                    if (sc == SDL_SCANCODE_ESCAPE) running = false;
                    else if (sc == SDL_SCANCODE_F11 || (sc == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)))
                    {
                        cfg.fullscreen = !cfg.fullscreen;
                        SDL_SetWindowFullscreen(win, cfg.fullscreen);
                    }
                    else if (sc == SDL_SCANCODE_F5) g_core->SaveState(state.c_str());
                    else if (sc == SDL_SCANCODE_F7 && file_exists(state)) g_core->LoadState(state.c_str());
                    else if (sc == SDL_SCANCODE_F2) cfg.smooth = !cfg.smooth;
                    else if (sc == SDL_SCANCODE_F9) { cfg.xlat = !cfg.xlat; rondo_xlat_attach(g_core, cfg.xlat); }
                    else if (sc == SDL_SCANCODE_P) paused = !paused;
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                bool down = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                u16 k = key_for_button(e.gbutton.button);
                if (k) { if (down) g_pad_buttons |= k; else g_pad_buttons &= ~k; }
                // North button: subweapon (Up + I)
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH)
                    g_pad_macro = down ? (GG_KEY_UP | GG_KEY_I) : 0;
                update_pad_state();
                break;
            }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            {
                const int dz = 16000;
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX)
                {
                    g_pad_axes &= ~(GG_KEY_LEFT | GG_KEY_RIGHT);
                    if (e.gaxis.value < -dz) g_pad_axes |= GG_KEY_LEFT;
                    if (e.gaxis.value > dz) g_pad_axes |= GG_KEY_RIGHT;
                }
                else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
                {
                    g_pad_axes &= ~(GG_KEY_UP | GG_KEY_DOWN);
                    if (e.gaxis.value < -dz) g_pad_axes |= GG_KEY_UP;
                    if (e.gaxis.value > dz) g_pad_axes |= GG_KEY_DOWN;
                }
                update_pad_state();
                break;
            }
            }
        }
        // Press RUN on the System Card title so the game boots straight away.
        static int boot_frame = 0;
        if (boot_frame < 200)
        {
            boot_frame++;
            if (boot_frame == 90) g_kb.state |= GG_KEY_RUN;
            if (boot_frame == 96) g_kb.state &= ~GG_KEY_RUN;
        }
        sync_input();

        if (!paused)
        {
            int runs = fast ? 4 : 1;
            for (int r = 0; r < runs; r++)
            {
                int count = 0;
                g_core->RunToVBlank(fb.data(), snd.data(), &count, NULL, r == runs - 1);
                if (audio && !fast && count > 0)
                    SDL_PutAudioStreamData(audio, snd.data(), count * (int)sizeof(s16));
                frames++;
            }
        }

        GG_Runtime_Info ri;
        g_core->GetRuntimeInfo(ri);
        if (!tex || ri.screen_width != tex_w || ri.screen_height != tex_h)
        {
            if (tex) SDL_DestroyTexture(tex);
            tex_w = ri.screen_width; tex_h = ri.screen_height;
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);
        }
        SDL_SetTextureScaleMode(tex, cfg.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_UpdateTexture(tex, NULL, fb.data(), tex_w * 4);
        int ww, wh;
        SDL_GetRenderOutputSize(ren, &ww, &wh);
        float aspect = ri.aspect_ratio > 0 ? ri.aspect_ratio : 4.0f / 3.0f;
        float dh = (float)wh, dw = dh * aspect;
        if (dw > ww) { dw = (float)ww; dh = dw / aspect; }
        SDL_FRect dst = {(ww - dw) * 0.5f, (wh - dh) * 0.5f, dw, dh};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, &dst);
        SDL_RenderPresent(ren);

        Uint64 now_ms = SDL_GetTicks();
        if (now_ms - last_title >= 1000)
        {
            unsigned long long x = RondoXlat::s_instructions, m = RondoXlat::s_misses;
            unsigned long long dx = x - prev_x, dm = m - prev_m;
            prev_x = x; prev_m = m;
            char title[160];
            double pct = (dx + dm) ? 100.0 * (double)dx / (double)(dx + dm) : 0.0;
            snprintf(title, sizeof(title), "rondo-nx (%s)  |  %d fps  |  %s: %.1f%% of instructions%s",
                japanese ? "Japanese" : "English", frames, cfg.xlat ? "translated" : "interpreter only", cfg.xlat ? pct : 0.0, paused ? "  |  PAUSED" : "");
            SDL_SetWindowTitle(win, title);
            frames = 0;
            last_title = now_ms;
        }
        if (now_ms - last_bram > 30000)
        {
            g_core->SaveRam(bram.c_str(), true);
            last_bram = now_ms;
        }

        // Pace to the PC Engine's 59.83 Hz, nudged by how much audio is queued.
        if (!fast)
        {
            next += (Uint64)frame_ns;
            if (audio)
            {
                int queued = SDL_GetAudioStreamQueued(audio);
                int per_frame = (int)(GG_AUDIO_SAMPLE_RATE / 59.826) * 4;
                if (queued > per_frame * 6) next += 1000000;
                else if (queued < per_frame * 2 && next > 1000000) next -= 1000000;
            }
            Uint64 now = SDL_GetTicksNS();
            if (next > now) SDL_DelayPrecise(next - now);
            else if (now - next > 100000000) next = now;
        }
        else
            next = SDL_GetTicksNS();
    }

    g_core->SaveRam(bram.c_str(), true);
    if (audio) SDL_DestroyAudioStream(audio);
    SDL_Quit();
    return 0;
}
