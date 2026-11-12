// rondo-nx: standalone runner for Castlevania: Rondo of Blood (PC Engine CD).
// Game code runs as translated C++ (generated from coverage traces); anything
// not translated falls back to the HuC6280 interpreter. Video, sound and CD
// hardware come from the Geargrafx core (GPL-3.0) for this first milestone.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>
#include "geargrafx.h"
#include "rondo_xlat_rt.h"
#include "rondo_wide.h"
#include "huc6270.h"
#include "menu.h"

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

static std::string read_file(const std::string& path)
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

static void write_file(const std::string& path, const std::string& text)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
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
    std::string text = read_file(join(g_base, "rondo-nx.ini"));
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string l = trim(text.substr(pos, end - pos));
        pos = end + 1;
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
}

// ------------------------------------------------------------------ menu host (PC)

static const char* pad_button_name(int b)
{
    switch (b)
    {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A";
    case SDL_GAMEPAD_BUTTON_EAST: return "B";
    case SDL_GAMEPAD_BUTTON_WEST: return "X";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
    case SDL_GAMEPAD_BUTTON_BACK: return "Back";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-Up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-Down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-Right";
    case 100: return "LT";
    case 101: return "RT";
    default: return "?";
    }
}

static std::string g_saves_dir;
static bool g_japanese = false;

static std::string state_path(int slot)
{
    char name[48];
    snprintf(name, sizeof(name), "%s.state%d", g_japanese ? "rondo" : "rondo-en", slot);
    return join(g_saves_dir, name);
}

struct PcHost : public MenuHost
{
    bool HasKeyboard() { return true; }
    bool HasWindow() { return true; }
    std::string KeyName(int code)
    {
        const char* n = SDL_GetScancodeName((SDL_Scancode)code);
        return (n && n[0]) ? n : "?";
    }
    std::string PadName(int code) { return pad_button_name(code); }
    std::string SlotInfo(int slot)
    {
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(state_path(slot).c_str(), &info) || info.type != SDL_PATHTYPE_FILE) return "Empty";
        time_t t = (time_t)(info.modify_time / 1000000000LL);
        struct tm* lt = localtime(&t);
        char buf[32];
        strftime(buf, sizeof(buf), "%m/%d %H:%M", lt);
        return buf;
    }
    void DefaultBindings(MenuSettings& s)
    {
        s.kb[BIND_UP] = SDL_SCANCODE_UP;
        s.kb[BIND_DOWN] = SDL_SCANCODE_DOWN;
        s.kb[BIND_LEFT] = SDL_SCANCODE_LEFT;
        s.kb[BIND_RIGHT] = SDL_SCANCODE_RIGHT;
        s.kb[BIND_ATTACK] = SDL_SCANCODE_X;
        s.kb[BIND_JUMP] = SDL_SCANCODE_Z;
        s.kb[BIND_SUBWEAPON] = SDL_SCANCODE_C;
        s.kb[BIND_RUN] = SDL_SCANCODE_RETURN;
        s.kb[BIND_SELECT] = SDL_SCANCODE_RSHIFT;
        s.pad[BIND_UP] = SDL_GAMEPAD_BUTTON_DPAD_UP;
        s.pad[BIND_DOWN] = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        s.pad[BIND_LEFT] = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
        s.pad[BIND_RIGHT] = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        s.pad[BIND_ATTACK] = SDL_GAMEPAD_BUTTON_WEST;
        s.pad[BIND_JUMP] = SDL_GAMEPAD_BUTTON_SOUTH;
        s.pad[BIND_SUBWEAPON] = SDL_GAMEPAD_BUTTON_NORTH;
        s.pad[BIND_RUN] = SDL_GAMEPAD_BUTTON_START;
        s.pad[BIND_SELECT] = SDL_GAMEPAD_BUTTON_BACK;
    }
};

// ------------------------------------------------------------------ input

static GeargrafxCore* g_core;
static u16 g_sent = 0;
static bool g_kb_action[BIND_COUNT];
static bool g_pad_action[BIND_COUNT];
static u16 g_pad_axes = 0;

static u16 action_keys(const bool* act)
{
    u16 k = 0;
    if (act[BIND_UP]) k |= GG_KEY_UP;
    if (act[BIND_DOWN]) k |= GG_KEY_DOWN;
    if (act[BIND_LEFT]) k |= GG_KEY_LEFT;
    if (act[BIND_RIGHT]) k |= GG_KEY_RIGHT;
    if (act[BIND_ATTACK]) k |= GG_KEY_II;   // Rondo attacks on II
    if (act[BIND_JUMP]) k |= GG_KEY_I;      // and jumps on I
    if (act[BIND_SUBWEAPON]) k |= GG_KEY_UP | GG_KEY_II;
    if (act[BIND_RUN]) k |= GG_KEY_RUN;
    if (act[BIND_SELECT]) k |= GG_KEY_SELECT;
    return k;
}

static void sync_input(u16 want)
{
    static const GG_Keys keys[] = {GG_KEY_I, GG_KEY_II, GG_KEY_SELECT, GG_KEY_RUN, GG_KEY_UP, GG_KEY_RIGHT, GG_KEY_DOWN, GG_KEY_LEFT};
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
        u16 k = (u16)keys[i];
        if ((want & k) && !(g_sent & k)) g_core->KeyPressed(GG_CONTROLLER_1, keys[i]);
        if (!(want & k) && (g_sent & k)) g_core->KeyReleased(GG_CONTROLLER_1, keys[i]);
    }
    g_sent = want;
}

static void clear_actions()
{
    for (int i = 0; i < BIND_COUNT; i++) { g_kb_action[i] = false; g_pad_action[i] = false; }
    g_pad_axes = 0;
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
    bool lang_from_args = false;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--interpreted")) cfg.xlat = false;
        else if (!strcmp(argv[i], "--fullscreen")) cfg.fullscreen = true;
        else if (!strcmp(argv[i], "--japanese")) { cfg.language = "japanese"; lang_from_args = true; }
        else if (!strcmp(argv[i], "--english")) { cfg.language = "english"; lang_from_args = true; }
    }
    if (cfg.saves.empty()) cfg.saves = join(g_base, "saves");
    SDL_CreateDirectory(cfg.saves.c_str());
    g_saves_dir = cfg.saves;

    // Settings changed in the menu live in saves/settings.ini; rondo-nx.ini supplies the first defaults.
    PcHost host;
    MenuSettings ms;
    host.DefaultBindings(ms);
    ms.scale = cfg.scale;
    ms.fullscreen = cfg.fullscreen;
    ms.smooth = cfg.smooth;
    ms.language = (cfg.language == "japanese" || cfg.language == "jp") ? 1 : 0;
    std::string settings_path = join(cfg.saves, "settings.ini");
    std::string saved = read_file(settings_path);
    if (!saved.empty()) ms.Parse(saved);
    if (lang_from_args) ms.language = (cfg.language == "japanese") ? 1 : 0;
    Menu menu(&host, &ms);

    std::string cue_for[2] = { !cfg.cue_english.empty() ? cfg.cue_english : cfg.cue,
                               !cfg.cue_japanese.empty() ? cfg.cue_japanese : cfg.cue };
    if (!file_exists(cue_for[ms.language]) && file_exists(cue_for[1 - ms.language]))
        ms.language = 1 - ms.language;   // only one disc configured
    g_japanese = (ms.language == 1);
    std::string cue = cue_for[ms.language];

    if (cue.empty() || !file_exists(cue) || cfg.bios.empty() || !file_exists(cfg.bios))
    {
        std::string msg = "Couldn't find the game files.\n\nEdit rondo-nx.ini next to rondo-nx.exe and set:\n"
                          "cue_english=<path to the English .cue file>\ncue_japanese=<path to the Japanese .cue file>\nbios=<path to syscard3.pce>\n\nCurrently:\ncue=" + cue + "\nbios=" + cfg.bios;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "rondo-nx", msg.c_str(), NULL);
        return 1;
    }

    g_core = new GeargrafxCore();
    g_core->Init(NULL, GG_PIXEL_RGBA8888);
    if (!g_core->LoadBios(cfg.bios.c_str(), true) || !g_core->LoadMedia(cue.c_str()))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "rondo-nx", "The emulated CD or System Card failed to load. Check the paths in rondo-nx.ini.", NULL);
        return 1;
    }
    // Separate saves per language: the two discs keep different save data.
    std::string bram = join(cfg.saves, g_japanese ? "rondo.sav" : "rondo-en.sav");
    BackupSaveFile(bram);
    if (file_exists(bram)) g_core->LoadRam(bram.c_str(), true);
    rondo_xlat_attach(g_core, cfg.xlat);

    SDL_Window* win = SDL_CreateWindow("rondo-nx", 320 * ms.scale, 240 * ms.scale, SDL_WINDOW_RESIZABLE | (ms.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    SDL_Renderer* ren = SDL_CreateRenderer(win, NULL);
    SDL_SetRenderVSync(ren, 0);
    SDL_Texture* tex = NULL;
    int tex_w = 0, tex_h = 0;
    SDL_Texture* overlay = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, Menu::W, Menu::H);
    SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(overlay, SDL_SCALEMODE_NEAREST);
    std::vector<uint8_t> overlay_px(Menu::W * Menu::H * 4);

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = GG_AUDIO_SAMPLE_RATE;
    SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio) { SDL_SetAudioStreamGain(audio, ms.volume / 10.0f); SDL_ResumeAudioStreamDevice(audio); }

    std::vector<u8> fb(2048 * 512 * 4);
    std::vector<s16> snd(GG_AUDIO_BUFFER_SIZE * 4);

    const double frame_ns = 1e9 / 59.826;
    Uint64 next = SDL_GetTicksNS();
    Uint64 last_title = 0, last_bram = SDL_GetTicks();
    double perf_sum = 0, perf_max = 0;
    int perf_n = 0;
    int frames = 0;
    int boot_frame = 0;
    bool running = true, fast = false;
    unsigned long long prev_x = 0, prev_m = 0;
    bool lt_down = false, rt_down = false;

    auto save_settings = [&]() { write_file(settings_path, ms.Serialize()); };

    auto apply_video = [&]()
    {
        SDL_SetWindowFullscreen(win, ms.fullscreen);
        if (!ms.fullscreen) SDL_SetWindowSize(win, 320 * ms.scale, 240 * ms.scale);
    };

    std::vector<u8> wide;
    int give_sub = -1;   // subweapon waiting to be handed over (applied during gameplay)
    auto handle = [&](MenuResult r)
    {
        switch (r)
        {
        case MR_RESET:
            g_core->ResetMedia(true);
            boot_frame = 0;
            menu.Toast("Reset");
            break;
        case MR_SAVE_STATE:
        {
            char t[32];
            bool ok = g_core->SaveState(state_path(ms.slot).c_str());
            snprintf(t, sizeof(t), ok ? "Saved to slot %d" : "Couldn't save slot %d", ms.slot);
            menu.Toast(t);
            break;
        }
        case MR_LOAD_STATE:
        {
            char t[32];
            bool ok = g_core->LoadState(state_path(ms.slot).c_str());
            snprintf(t, sizeof(t), ok ? "Loaded slot %d" : "Couldn't load slot %d", ms.slot);
            menu.Toast(t);
            break;
        }
        case MR_LANGUAGE:
        {
            std::string next_cue = cue_for[ms.language];
            if (!file_exists(next_cue))
            {
                ms.language = 1 - ms.language;
                menu.Toast(ms.language ? "English disc not found" : "Japanese disc not found");
                break;
            }
            g_core->SaveRam(bram.c_str(), true);
            g_japanese = (ms.language == 1);
            bram = join(cfg.saves, g_japanese ? "rondo.sav" : "rondo-en.sav");
            if (g_core->LoadMedia(next_cue.c_str()))
            {
                BackupSaveFile(bram);
                if (file_exists(bram)) g_core->LoadRam(bram.c_str(), true);
                g_core->ResetMedia(true);
                boot_frame = 0;
                menu.Toast(g_japanese ? "Japanese" : "English");
            }
            break;
        }
        case MR_VIDEO: apply_video(); break;
        case MR_SUBWEAPON:
            // Swap it in right away when a stage is loaded (the game is paused behind the menu, so
            // "Holding now" shows it at once); otherwise it's handed over when the next stage starts.
            give_sub = ms.subweapon;
            if (ApplyCheats(g_core->GetMemory()->GetMemoryMap()[0xF8], ms, give_sub))
            {
                menu.Toast(std::string("Subweapon: ") + SubweaponName(give_sub));
                give_sub = -1;
            }
            menu.SetCurrentSubweapon(g_core->GetMemory()->GetMemoryMap()[0xF8][0x99] ? g_core->GetMemory()->GetMemoryMap()[0xF8][0x93] : -1);
            break;
        case MR_VOLUME: if (audio) SDL_SetAudioStreamGain(audio, ms.volume / 10.0f); break;
        case MR_QUIT: running = false; break;
        default: break;
        }
        if (r != MR_NONE) save_settings();
    };

    auto open_menu = [&]()
    {
        menu.Open();
        clear_actions();
        sync_input(0);
    };

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
                if (menu.IsOpen())
                {
                    if (!down || e.key.repeat) break;
                    if (menu.IsCapturing())
                    {
                        if (menu.CapturingKeyboard()) handle(menu.Captured(sc == SDL_SCANCODE_ESCAPE ? -1 : (int)sc));
                        break;
                    }
                    if (sc == SDL_SCANCODE_UP) handle(menu.Nav(NAV_UP));
                    else if (sc == SDL_SCANCODE_DOWN) handle(menu.Nav(NAV_DOWN));
                    else if (sc == SDL_SCANCODE_LEFT) handle(menu.Nav(NAV_LEFT));
                    else if (sc == SDL_SCANCODE_RIGHT) handle(menu.Nav(NAV_RIGHT));
                    else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_SPACE) handle(menu.Nav(NAV_OK));
                    else if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_BACKSPACE) handle(menu.Nav(NAV_BACK));
                    break;
                }
                for (int a = 0; a < BIND_COUNT; a++)
                    if (ms.kb[a] == (int)sc) g_kb_action[a] = down;
                if (sc == SDL_SCANCODE_TAB) fast = down;
                if (down && !e.key.repeat)
                {
                    if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_F1) open_menu();
                    else if (sc == SDL_SCANCODE_F11 || (sc == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)))
                    {
                        ms.fullscreen = !ms.fullscreen;
                        apply_video();
                        save_settings();
                    }
                    else if (sc == SDL_SCANCODE_F5) handle(MR_SAVE_STATE);
                    else if (sc == SDL_SCANCODE_F7 && host.SlotInfo(ms.slot) != "Empty") handle(MR_LOAD_STATE);
                    else if (sc == SDL_SCANCODE_F9)
                    {
                        cfg.xlat = !cfg.xlat;
                        rondo_xlat_attach(g_core, cfg.xlat);
                        menu.Toast(cfg.xlat ? "Translated code" : "Interpreter only");
                    }
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                bool down = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                int b = e.gbutton.button;
                if (menu.IsOpen())
                {
                    if (!down) break;
                    if (menu.IsCapturing())
                    {
                        if (!menu.CapturingKeyboard()) handle(menu.Captured(b));
                        break;
                    }
                    if (b == SDL_GAMEPAD_BUTTON_DPAD_UP) handle(menu.Nav(NAV_UP));
                    else if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) handle(menu.Nav(NAV_DOWN));
                    else if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) handle(menu.Nav(NAV_LEFT));
                    else if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) handle(menu.Nav(NAV_RIGHT));
                    else if (b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START) handle(menu.Nav(NAV_OK));
                    else if (b == SDL_GAMEPAD_BUTTON_EAST) handle(menu.Nav(NAV_BACK));
                    else if (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK || b == SDL_GAMEPAD_BUTTON_GUIDE) handle(menu.Close());
                    break;
                }
                if (down && (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK || b == SDL_GAMEPAD_BUTTON_GUIDE)) { open_menu(); break; }
                for (int a = 0; a < BIND_COUNT; a++)
                    if (ms.pad[a] == b) g_pad_action[a] = down;
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
                else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                {
                    // Triggers act as buttons 100 (LT) and 101 (RT) so they can be bound.
                    int code = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 100 : 101;
                    bool& was = code == 100 ? lt_down : rt_down;
                    bool now = e.gaxis.value > dz;
                    if (now == was) break;
                    was = now;
                    if (menu.IsOpen())
                    {
                        if (now && menu.IsCapturing() && !menu.CapturingKeyboard()) handle(menu.Captured(code));
                        break;
                    }
                    for (int a = 0; a < BIND_COUNT; a++)
                        if (ms.pad[a] == code) g_pad_action[a] = now;
                }
                break;
            }
            }
        }

        if (!menu.IsOpen())
        {
            u16 want = action_keys(g_kb_action) | action_keys(g_pad_action) | g_pad_axes;
            // Press RUN on the System Card title so the game boots straight away.
            if (boot_frame < 200)
            {
                boot_frame++;
                if (boot_frame >= 90 && boot_frame < 96) want |= GG_KEY_RUN;
            }
            sync_input(want);

            // Widescreen: the game keeps sprite pieces that reach into the side areas, and the
            // chip's 16 sprites per line limit is lifted (those extra pieces would count against it).
            RondoXlat::s_wide_sprites = (ms.aspect == 2);
            g_core->GetHuC6270_1()->SetNoSpriteLimit(ms.aspect == 2);
            int runs = fast ? 4 : 1;
            for (int r = 0; r < runs; r++)
            {
                int count = 0;
                Uint64 t0 = SDL_GetPerformanceCounter();
                g_core->RunToVBlank(fb.data(), snd.data(), &count, NULL, r == runs - 1);
                double ms_f = (SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
                perf_sum += ms_f;
                if (ms_f > perf_max) perf_max = ms_f;
                perf_n++;
                if (ApplyCheats(g_core->GetMemory()->GetMemoryMap()[0xF8], ms, give_sub))
                {
                    menu.Toast(std::string("Subweapon: ") + SubweaponName(give_sub));
                    give_sub = -1;
                }
                if (audio && !fast && count > 0)
                    SDL_PutAudioStreamData(audio, snd.data(), count * (int)sizeof(s16));
                frames++;
            }
        }
        menu.Tick();
        {
            const u8* w = g_core->GetMemory()->GetMemoryMap()[0xF8];
            menu.SetCurrentSubweapon(w[0x99] ? w[0x93] : -1);
        }

        GG_Runtime_Info ri;
        g_core->GetRuntimeInfo(ri);
        // Widescreen: extra scenery drawn at both sides during gameplay, black bars elsewhere.
        const bool widescreen = ms.aspect == 2;
        const int pic_w = widescreen ? ri.screen_width + 2 * RondoWide::kExtra : ri.screen_width;
        const u8* pic = fb.data();
        if (widescreen)
        {
            wide.resize((size_t)pic_w * ri.screen_height * 4);
            RondoWide::Compose(g_core, fb.data(), ri.screen_width, ri.screen_height, wide.data(),
                g_core->GetMemory()->GetMemoryMap()[0xF8][0x99] != 0, 0);
            pic = wide.data();
        }
        if (!tex || pic_w != tex_w || ri.screen_height != tex_h)
        {
            if (tex) SDL_DestroyTexture(tex);
            tex_w = pic_w; tex_h = ri.screen_height;
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);
        }
        SDL_SetTextureScaleMode(tex, ms.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_UpdateTexture(tex, NULL, pic, tex_w * 4);
        int ww, wh;
        SDL_GetRenderOutputSize(ren, &ww, &wh);
        SDL_FRect dst;
        if (ms.aspect == 1)
            dst = {0, 0, (float)ww, (float)wh};
        else
        {
            float aspect = ri.aspect_ratio > 0 ? ri.aspect_ratio : 4.0f / 3.0f;
            aspect = aspect * pic_w / ri.screen_width;
            float dh = (float)wh, dw = dh * aspect;
            if (dw > ww) { dw = (float)ww; dh = dw / aspect; }
            dst = {(ww - dw) * 0.5f, (wh - dh) * 0.5f, dw, dh};
        }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, &dst);
        if (menu.Visible())
        {
            menu.Draw(overlay_px.data());
            SDL_UpdateTexture(overlay, NULL, overlay_px.data(), Menu::W * 4);
            SDL_FRect odst = dst;
            if (widescreen) { odst.w = dst.w * ri.screen_width / pic_w; odst.x = dst.x + (dst.w - odst.w) * 0.5f; }
            SDL_RenderTexture(ren, overlay, NULL, &odst);
        }
        SDL_RenderPresent(ren);

        Uint64 now_ms = SDL_GetTicks();
        if (now_ms - last_title >= 1000)
        {
            unsigned long long x = RondoXlat::s_instructions, m = RondoXlat::s_misses;
            unsigned long long dx = x - prev_x, dm = m - prev_m;
            prev_x = x; prev_m = m;
            char title[160];
            double pct = (dx + dm) ? 100.0 * (double)dx / (double)(dx + dm) : 0.0;
            if (menu.IsOpen())
                snprintf(title, sizeof(title), "rondo-nx (%s)  |  menu", g_japanese ? "Japanese" : "English");
            else
                snprintf(title, sizeof(title), "rondo-nx (%s)  |  %d fps  |  %s: %.1f%% of instructions",
                    g_japanese ? "Japanese" : "English", frames, cfg.xlat ? "translated" : "interpreter only", cfg.xlat ? pct : 0.0);
            SDL_SetWindowTitle(win, title);
            char hud[48];
            snprintf(hud, sizeof(hud), "%2dfps %4.1f/%4.1fms %4.1f%%", frames, perf_n ? perf_sum / perf_n : 0.0, perf_max,
                cfg.xlat ? pct : 0.0);
            menu.SetHud(hud);
            perf_sum = 0; perf_max = 0; perf_n = 0;
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
            if (audio && !menu.IsOpen())
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
    save_settings();
    if (audio) SDL_DestroyAudioStream(audio);
    SDL_Quit();
    return 0;
}
