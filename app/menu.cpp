#include "menu.h"
#include "font8x8_basic.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

static const char* k_bind_names[BIND_COUNT] = {
    "Up", "Down", "Left", "Right", "Attack (II)", "Jump (I)", "Subweapon", "Run", "Select"};
static const char* k_bind_keys[BIND_COUNT] = {
    "up", "down", "left", "right", "attack", "jump", "subweapon", "run", "select"};

const char* BindActionName(int a) { return (a >= 0 && a < BIND_COUNT) ? k_bind_names[a] : "?"; }

// ------------------------------------------------------------------ settings

std::string MenuSettings::Serialize() const
{
    std::string s;
    char line[256];
    snprintf(line, sizeof(line), "slot=%d\nvolume=%d\naspect=%d\nsmooth=%d\nfullscreen=%d\nscale=%d\nlanguage=%s\nperf=%d\ncheat_hp=%d\ncheat_hearts=%d\nsubweapon=%d\n",
        slot, volume, aspect, smooth ? 1 : 0, fullscreen ? 1 : 0, scale, language ? "japanese" : "english", perf ? 1 : 0,
        cheat_hp ? 1 : 0, cheat_hearts ? 1 : 0, subweapon);
    s += line;
    s += "bindings=2\n";
    for (int i = 0; i < BIND_COUNT; i++)
    {
        snprintf(line, sizeof(line), "kb_%s=%d\npad_%s=%d\n", k_bind_keys[i], kb[i], k_bind_keys[i], pad[i]);
        s += line;
    }
    return s;
}

void MenuSettings::Parse(const std::string& text)
{
    // Bindings version 1 (the first builds) had Attack and Jump wired to the wrong
    // PC Engine buttons (the game jumps on I and attacks on II), so those entries
    // are dropped and the defaults kept.
    bool old_bindings = text.find("bindings=2") == std::string::npos;
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string l = text.substr(pos, end - pos);
        pos = end + 1;
        while (!l.empty() && (l[l.size() - 1] == '\r' || l[l.size() - 1] == ' ')) l.erase(l.size() - 1);
        size_t eq = l.find('=');
        if (eq == std::string::npos || l[0] == '#') continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        int n = atoi(v.c_str());
        if (k == "slot") slot = (n >= 1 && n <= 5) ? n : 1;
        else if (k == "volume") volume = (n >= 0 && n <= 10) ? n : 10;
        else if (k == "aspect") aspect = (n >= 0 && n <= 2) ? n : 0;
        else if (k == "smooth") smooth = (n != 0);
        else if (k == "fullscreen") fullscreen = (n != 0);
        else if (k == "scale") scale = (n >= 1 && n <= 8) ? n : 4;
        else if (k == "perf") perf = (n != 0);
        else if (k == "cheat_hp") cheat_hp = (n != 0);
        else if (k == "cheat_hearts") cheat_hearts = (n != 0);
        else if (k == "subweapon") subweapon = (n >= 0 && n < SUBWEAPON_COUNT) ? n : 0;
        else if (k == "language") language = (v == "japanese" || v == "jp") ? 1 : 0;
        else
        {
            for (int i = 0; i < BIND_COUNT; i++)
            {
                if (old_bindings && (i == BIND_ATTACK || i == BIND_JUMP || i == BIND_SUBWEAPON)) continue;
                if (k == std::string("kb_") + k_bind_keys[i]) kb[i] = n;
                if (k == std::string("pad_") + k_bind_keys[i]) pad[i] = n;
            }
        }
    }
}

// ------------------------------------------------------------------ menu logic

enum MainItem { MI_RESUME, MI_RESET, MI_SAVE, MI_LOAD, MI_CONTROLS, MI_OPTIONS, MI_CHEATS, MI_QUIT, MI_COUNT };
enum CheatItem { CI_HP, CI_HEARTS, CI_SUBWEAPON, CI_BACK, CI_COUNT };
enum OptItem { OI_LANGUAGE, OI_VOLUME, OI_ASPECT, OI_PIXELS, OI_FULLSCREEN, OI_SCALE, OI_PERF, OI_BACK };

static std::vector<int> options_items(MenuHost* h)
{
    std::vector<int> v;
    v.push_back(OI_LANGUAGE);
    v.push_back(OI_VOLUME);
    v.push_back(OI_ASPECT);
    if (h->HasWindow()) { v.push_back(OI_PIXELS); v.push_back(OI_FULLSCREEN); v.push_back(OI_SCALE); }
    v.push_back(OI_PERF);
    v.push_back(OI_BACK);
    return v;
}

void Menu::Open()
{
    m_open = true;
    m_page = PAGE_MAIN;
    m_sel = 0;
    m_capture = -1;
    m_lang_pending = -1;
}

MenuResult Menu::Close()
{
    m_open = false;
    m_capture = -1;
    return MR_RESUME;
}

int Menu::ItemCount() const
{
    switch (m_page)
    {
    case PAGE_MAIN: return MI_COUNT;
    case PAGE_CONTROLS: return BIND_COUNT + 2;
    case PAGE_OPTIONS: return (int)options_items(m_host).size();
    case PAGE_CHEATS: return CI_COUNT;
    }
    return 1;
}

void Menu::Toast(const std::string& text)
{
    m_toast = text;
    m_toast_frames = 120;
}

void Menu::Tick()
{
    if (m_toast_frames > 0) m_toast_frames--;
    if (m_capture >= 0 && --m_capture_frames <= 0)
        m_capture = -1;
}

MenuResult Menu::Nav(MenuNav n)
{
    if (!m_open || m_capture >= 0)
        return MR_NONE;
    int count = ItemCount();
    switch (n)
    {
    case NAV_UP: m_sel = (m_sel + count - 1) % count; m_lang_pending = -1; return MR_NONE;
    case NAV_DOWN: m_sel = (m_sel + 1) % count; m_lang_pending = -1; return MR_NONE;
    case NAV_LEFT: return Activate(-1);
    case NAV_RIGHT: return Activate(+1);
    case NAV_OK: return Activate(0);
    case NAV_BACK:
        if (m_page == PAGE_MAIN) return Close();
        m_sel = (m_page == PAGE_CONTROLS) ? MI_CONTROLS : (m_page == PAGE_CHEATS ? MI_CHEATS : MI_OPTIONS);
        m_page = PAGE_MAIN;
        return MR_NONE;
    }
    return MR_NONE;
}

// dir: -1/+1 for left/right, 0 for OK
MenuResult Menu::Activate(int dir)
{
    if (m_page == PAGE_MAIN)
    {
        switch (m_sel)
        {
        case MI_RESUME: return dir == 0 ? Close() : MR_NONE;
        case MI_RESET:
            if (dir != 0) return MR_NONE;
            Close();
            return MR_RESET;
        case MI_SAVE:
        case MI_LOAD:
            if (dir != 0) { m_s->slot = (m_s->slot - 1 + dir + 5) % 5 + 1; return MR_NONE; }
            if (m_sel == MI_LOAD && m_host->SlotInfo(m_s->slot) == "Empty") { Toast("Slot is empty"); return MR_NONE; }
            Close();
            return m_sel == MI_SAVE ? MR_SAVE_STATE : MR_LOAD_STATE;
        case MI_CONTROLS:
            if (dir != 0) return MR_NONE;
            m_page = PAGE_CONTROLS; m_sel = 0; m_col = m_host->HasKeyboard() ? 0 : 1;
            return MR_NONE;
        case MI_OPTIONS:
            if (dir != 0) return MR_NONE;
            m_page = PAGE_OPTIONS; m_sel = 0;
            return MR_NONE;
        case MI_CHEATS:
            if (dir != 0) return MR_NONE;
            m_page = PAGE_CHEATS; m_sel = 0;
            return MR_NONE;
        case MI_QUIT: return dir == 0 ? MR_QUIT : MR_NONE;
        }
        return MR_NONE;
    }

    if (m_page == PAGE_CHEATS)
    {
        switch (m_sel)
        {
        case CI_HP: m_s->cheat_hp = !m_s->cheat_hp; return MR_BINDINGS;
        case CI_HEARTS: m_s->cheat_hearts = !m_s->cheat_hearts; return MR_BINDINGS;
        case CI_SUBWEAPON:
            if (dir != 0) { m_s->subweapon = (m_s->subweapon + dir + SUBWEAPON_COUNT) % SUBWEAPON_COUNT; return MR_BINDINGS; }
            return MR_SUBWEAPON;
        case CI_BACK:
            if (dir == 0) { m_page = PAGE_MAIN; m_sel = MI_CHEATS; }
            return MR_NONE;
        }
        return MR_NONE;
    }

    if (m_page == PAGE_CONTROLS)
    {
        if (m_sel < BIND_COUNT)
        {
            if (dir != 0)
            {
                if (m_host->HasKeyboard()) m_col = m_col ? 0 : 1;
                return MR_NONE;
            }
            m_capture = m_sel;
            m_capture_col = m_col;
            m_capture_frames = 60 * 5;
            return MR_NONE;
        }
        if (m_sel == BIND_COUNT && dir == 0)
        {
            m_host->DefaultBindings(*m_s);
            Toast("Controls reset to defaults");
            return MR_BINDINGS;
        }
        if (m_sel == BIND_COUNT + 1 && dir == 0) { m_page = PAGE_MAIN; m_sel = MI_CONTROLS; }
        return MR_NONE;
    }

    // Options
    std::vector<int> items = options_items(m_host);
    int item = items[m_sel];
    switch (item)
    {
    case OI_LANGUAGE:
        if (dir != 0)
        {
            int cur = m_lang_pending >= 0 ? m_lang_pending : m_s->language;
            m_lang_pending = cur ? 0 : 1;
            if (m_lang_pending == m_s->language) m_lang_pending = -1;
            return MR_NONE;
        }
        if (m_lang_pending >= 0)
        {
            m_s->language = m_lang_pending;
            m_lang_pending = -1;
            Close();
            return MR_LANGUAGE;
        }
        return MR_NONE;
    case OI_VOLUME:
        if (dir == 0) return MR_NONE;
        m_s->volume += dir;
        if (m_s->volume < 0) m_s->volume = 0;
        if (m_s->volume > 10) m_s->volume = 10;
        return MR_VOLUME;
    case OI_ASPECT: m_s->aspect = (m_s->aspect + (dir < 0 ? 2 : 1)) % 3; return MR_VIDEO;
    case OI_PIXELS: m_s->smooth = !m_s->smooth; return MR_VIDEO;
    case OI_FULLSCREEN: m_s->fullscreen = !m_s->fullscreen; return MR_VIDEO;
    case OI_PERF: m_s->perf = !m_s->perf; return MR_VIDEO;
    case OI_SCALE:
        if (dir == 0) dir = 1;
        m_s->scale += dir;
        if (m_s->scale < 1) m_s->scale = 1;
        if (m_s->scale > 8) m_s->scale = 8;
        return MR_VIDEO;
    case OI_BACK:
        if (dir == 0) { m_page = PAGE_MAIN; m_sel = MI_OPTIONS; }
        return MR_NONE;
    }
    return MR_NONE;
}

MenuResult Menu::Captured(int code)
{
    if (m_capture < 0) return MR_NONE;
    int a = m_capture;
    m_capture = -1;
    if (code < 0) return MR_NONE;
    int* col = m_capture_col == 0 ? m_s->kb : m_s->pad;
    // Swap with any action already using this input, so nothing is left doubled up.
    for (int i = 0; i < BIND_COUNT; i++)
        if (i != a && col[i] == code) col[i] = col[a];
    col[a] = code;
    return MR_BINDINGS;
}

// ------------------------------------------------------------------ drawing

static const uint32_t C_WHITE = 0xE8E8E8FF, C_GRAY = 0x9090A0FF, C_GOLD = 0xF0C040FF,
                      C_RED = 0xD03030FF, C_SEL = 0xFFE070FF, C_DIM = 0x606070FF;

void Menu::Fill(uint8_t* p, int x, int y, int w, int h, uint32_t c)
{
    for (int j = y; j < y + h; j++)
    {
        if (j < 0 || j >= H) continue;
        for (int i = x; i < x + w; i++)
        {
            if (i < 0 || i >= W) continue;
            uint8_t* d = p + (j * W + i) * 4;
            d[0] = c >> 24; d[1] = c >> 16; d[2] = c >> 8; d[3] = c;
        }
    }
}

void Menu::Text(uint8_t* p, int x, int y, const std::string& s, uint32_t c)
{
    for (size_t k = 0; k < s.size(); k++)
    {
        unsigned char ch = (unsigned char)s[k];
        if (ch >= 128) ch = '?';
        const unsigned char* g = font8x8_basic[ch];
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                if (g[row] >> col & 1)
                {
                    // 1-pixel drop shadow keeps text readable over bright scenes
                    Fill(p, x + (int)k * 8 + col + 1, y + row + 1, 1, 1, 0x000000FF);
                    Fill(p, x + (int)k * 8 + col, y + row, 1, 1, c);
                }
    }
}

std::string Menu::BindName(int action, int col)
{
    int code = col == 0 ? m_s->kb[action] : m_s->pad[action];
    if (code < 0) return "-";
    std::string n = col == 0 ? m_host->KeyName(code) : m_host->PadName(code);
    // Shorten common long key names so they fit the column.
    if (n.compare(0, 6, "Right ") == 0) n = "R " + n.substr(6);
    else if (n.compare(0, 5, "Left ") == 0) n = "L " + n.substr(5);
    else if (n.compare(0, 7, "Keypad ") == 0) n = "Pad " + n.substr(7);
    return n;
}

static std::string fit(const std::string& s, size_t n)
{
    return s.size() <= n ? s : s.substr(0, n);
}

void Menu::Draw(uint8_t* p)
{
    memset(p, 0, W * H * 4);

    if (!m_open)
    {
        if (m_s->perf && !m_hud.empty())
        {
            Fill(p, 2, 2, (int)m_hud.size() * 8 + 4, 12, 0x000000B0);
            Text(p, 4, 4, m_hud, C_WHITE);
        }
        if (m_toast_frames > 0)
        {
            int w = (int)m_toast.size() * 8 + 16;
            Fill(p, (W - w) / 2, H - 28, w, 18, 0x101018D0);
            Text(p, (W - w) / 2 + 8, H - 23, m_toast, C_WHITE);
        }
        return;
    }

    Fill(p, 0, 0, W, H, 0x000000C8);
    Fill(p, 16, 12, W - 32, H - 24, 0x0A0A14F0);
    Fill(p, 16, 12, W - 32, 1, C_RED);
    Fill(p, 16, H - 13, W - 32, 1, C_RED);
    Text(p, 28, 20, "RONDO-NX", C_RED);

    const int x0 = 36, y0 = 48, dy = 14;
    if (m_page == PAGE_MAIN)
    {
        Text(p, 28 + 9 * 8, 20, "PAUSED", C_GOLD);
        const char* labels[MI_COUNT] = {"Resume", "Reset", "Save state", "Load state", "Controls", "Options", "Cheats", "Quit"};
        for (int i = 0; i < MI_COUNT; i++)
        {
            bool sel = (i == m_sel);
            uint32_t c = sel ? C_SEL : C_WHITE;
            if (sel) Text(p, x0 - 12, y0 + i * dy, ">", C_SEL);
            Text(p, x0, y0 + i * dy, labels[i], c);
            if (i == MI_SAVE || i == MI_LOAD)
            {
                char slot[32];
                snprintf(slot, sizeof(slot), "< %d >", m_s->slot);
                Text(p, x0 + 88, y0 + i * dy, slot, c);
                Text(p, x0 + 88 + 48, y0 + i * dy, fit(m_host->SlotInfo(m_s->slot), 14), C_GRAY);
            }
        }
        Text(p, 28, H - 36, "Left/Right: change save slot", C_DIM);
        Text(p, 28, H - 26, "OK: select  Back: resume", C_DIM);
        return;
    }

    if (m_page == PAGE_CHEATS)
    {
        Text(p, 28 + 9 * 8, 20, "CHEATS", C_GOLD);
        for (int i = 0; i < CI_COUNT; i++)
        {
            int y = y0 + i * dy;
            bool sel = (i == m_sel);
            uint32_t c = sel ? C_SEL : C_WHITE;
            if (sel) Text(p, x0 - 12, y, ">", C_SEL);
            std::string label, value;
            switch (i)
            {
            case CI_HP: label = "Infinite health"; value = m_s->cheat_hp ? "< On >" : "< Off >"; break;
            case CI_HEARTS: label = "Infinite hearts"; value = m_s->cheat_hearts ? "< On >" : "< Off >"; break;
            case CI_SUBWEAPON: label = "Subweapon"; value = std::string("< ") + SubweaponName(m_s->subweapon) + " >"; break;
            case CI_BACK: label = "Back"; break;
            }
            Text(p, x0, y, label, c);
            if (!value.empty()) Text(p, x0 + 136, y, value, c);
        }
        Text(p, 28, H - 46, "Subweapon: OK gives it now.", C_GRAY);
        Text(p, 28, H - 36, "Pits still kill. Made for Richter.", C_GRAY);
        Text(p, 28, H - 26, "Left/Right: change  Back: return", C_DIM);
        return;
    }

    if (m_page == PAGE_CONTROLS)
    {
        Text(p, 28 + 9 * 8, 20, "CONTROLS", C_GOLD);
        bool kb = m_host->HasKeyboard();
        int cx_kb = 128, cx_pad = kb ? 220 : 140;
        if (kb) Text(p, cx_kb, 34, "Keyboard", C_GRAY);
        Text(p, cx_pad, 34, "Controller", C_GRAY);
        for (int i = 0; i < BIND_COUNT + 2; i++)
        {
            int y = 46 + i * 14;
            bool sel = (i == m_sel);
            if (sel) Text(p, x0 - 12, y, ">", C_SEL);
            if (i < BIND_COUNT)
            {
                Text(p, x0, y, BindActionName(i), sel ? C_SEL : C_WHITE);
                if (kb)
                {
                    bool hl = sel && m_col == 0;
                    if (hl) Fill(p, cx_kb - 3, y - 3, 82, 14, 0x504020C0);
                    Text(p, cx_kb, y, fit(BindName(i, 0), 10), hl ? C_SEL : C_WHITE);
                }
                bool hl = sel && m_col == 1;
                if (hl) Fill(p, cx_pad - 3, y - 3, 82, 14, 0x504020C0);
                Text(p, cx_pad, y, fit(BindName(i, 1), 10), hl ? C_SEL : C_WHITE);
            }
            else
                Text(p, x0, y, i == BIND_COUNT ? "Reset to defaults" : "Back", sel ? C_SEL : C_WHITE);
        }
        if (m_capture >= 0)
        {
            Fill(p, 40, 92, W - 80, 52, 0x000000F0);
            Fill(p, 40, 92, W - 80, 1, C_GOLD);
            Fill(p, 40, 143, W - 80, 1, C_GOLD);
            std::string what = std::string(m_capture_col == 0 ? "Press a key for " : "Press a button for ") + BindActionName(m_capture);
            Text(p, (W - (int)what.size() * 8) / 2, 104, what, C_WHITE);
            char t[48];
            snprintf(t, sizeof(t), "(%d s to cancel)", m_capture_frames / 60 + 1);
            Text(p, (W - (int)strlen(t) * 8) / 2, 122, t, C_GRAY);
        }
        else
            Text(p, 28, H - 26, kb ? "Left/Right: column  OK: rebind" : "OK: rebind  Back: return", C_DIM);
        return;
    }

    // Options
    Text(p, 28 + 9 * 8, 20, "OPTIONS", C_GOLD);
    std::vector<int> items = options_items(m_host);
    for (size_t i = 0; i < items.size(); i++)
    {
        int y = y0 + (int)i * dy;
        bool sel = ((int)i == m_sel);
        uint32_t c = sel ? C_SEL : C_WHITE;
        if (sel) Text(p, x0 - 12, y, ">", C_SEL);
        std::string label, value;
        char buf[32];
        switch (items[i])
        {
        case OI_LANGUAGE:
        {
            label = "Language";
            int shown = m_lang_pending >= 0 ? m_lang_pending : m_s->language;
            value = shown ? "< Japanese >" : "< English >";
            break;
        }
        case OI_VOLUME: label = "Volume"; snprintf(buf, sizeof(buf), "< %d%% >", m_s->volume * 10); value = buf; break;
        case OI_ASPECT: label = "Aspect"; value = m_s->aspect == 2 ? "< Widescreen >" : (m_s->aspect ? "< Stretch >" : "< 4:3 >"); break;
        case OI_PIXELS: label = "Pixels"; value = m_s->smooth ? "< Smooth >" : "< Sharp >"; break;
        case OI_FULLSCREEN: label = "Fullscreen"; value = m_s->fullscreen ? "< On >" : "< Off >"; break;
        case OI_SCALE: label = "Window size"; snprintf(buf, sizeof(buf), "< %dx >", m_s->scale); value = buf; break;
        case OI_PERF: label = "Performance"; value = m_s->perf ? "< Show >" : "< Hide >"; break;
        case OI_BACK: label = "Back"; break;
        }
        Text(p, x0, y, label, c);
        if (!value.empty()) Text(p, x0 + 120, y, value, c);
    }
    if (m_lang_pending >= 0)
    {
        Text(p, 28, H - 36, "OK switches discs now.", C_GOLD);
        Text(p, 28, H - 26, "Unsaved progress is lost.", C_GOLD);
    }
    else
        Text(p, 28, H - 26, "Left/Right: change  Back: return", C_DIM);
}

// ------------------------------------------------------------------ save backups

void BackupSaveFile(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;
    std::vector<unsigned char> data;
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(f);
    bool any = false;
    for (size_t i = 0; i < data.size(); i++) if (data[i]) { any = true; break; }
    if (!any) return;

    size_t slash = path.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? std::string(".") : path.substr(0, slash);
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.rfind('.');
    std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    std::string bdir = dir + "/backups";
#ifdef _WIN32
    _mkdir(bdir.c_str());
#else
    mkdir(bdir.c_str(), 0777);
#endif
    time_t t = time(NULL);
    struct tm* lt = localtime(&t);
    char date[16];
    strftime(date, sizeof(date), "%Y%m%d", lt);
    std::string out = bdir + "/" + stem + "-" + date + ".sav";
    FILE* chk = fopen(out.c_str(), "rb");
    if (chk) { fclose(chk); return; }
    FILE* o = fopen(out.c_str(), "wb");
    if (!o) return;
    fwrite(data.data(), 1, data.size(), o);
    fclose(o);
}

// ------------------------------------------------------------------ cheats

static const char* k_sub_names[SUBWEAPON_COUNT] = {"Dagger", "Axe", "Holy Water", "Cross", "Bible", "Stopwatch", "Key"};
static const uint8_t k_sub_ids[SUBWEAPON_COUNT] = {0x05, 0x02, 0x04, 0x03, 0x08, 0x06, 0x07};

const char* SubweaponName(int i) { return (i >= 0 && i < SUBWEAPON_COUNT) ? k_sub_names[i] : "?"; }
uint8_t SubweaponId(int i) { return (i >= 0 && i < SUBWEAPON_COUNT) ? k_sub_ids[i] : 0; }

bool ApplyCheats(uint8_t* wram, const MenuSettings& s, int give_subweapon)
{
    if (!wram) return false;
    uint8_t max_hp = wram[0x99];
    if (max_hp == 0) return false;           // not in gameplay
    if (s.cheat_hp && wram[0x98] != 0 && wram[0x98] < max_hp) wram[0x98] = max_hp;
    if (s.cheat_hearts && wram[0x94] != 0x99) wram[0x94] = 0x99;
    if (give_subweapon >= 0) { wram[0x93] = SubweaponId(give_subweapon); return true; }
    return false;
}
