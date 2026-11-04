// rondo-nx in-game menu: platform-neutral. Draws into a 320x240 RGBA overlay
// (with alpha) that the PC and Switch front ends lay over the game picture.
#ifndef RONDO_MENU_H
#define RONDO_MENU_H

#include <string>
#include <stdint.h>

// Game actions the player can rebind.
enum BindAction
{
    BIND_UP, BIND_DOWN, BIND_LEFT, BIND_RIGHT,
    BIND_ATTACK, BIND_JUMP, BIND_SUBWEAPON, BIND_RUN, BIND_SELECT,
    BIND_COUNT
};

enum MenuNav { NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK };

// What the front end should do after a menu input.
enum MenuResult
{
    MR_NONE,
    MR_RESUME,          // menu closed
    MR_RESET,
    MR_SAVE_STATE,      // slot in settings.slot
    MR_LOAD_STATE,
    MR_LANGUAGE,        // settings.language changed: reload the disc
    MR_VIDEO,           // fullscreen/scale/smooth/aspect changed
    MR_VOLUME,
    MR_BINDINGS,        // a binding changed
    MR_SUBWEAPON,       // give settings.subweapon now
    MR_QUIT
};

struct MenuSettings
{
    int slot = 1;            // 1..5
    int volume = 10;         // 0..10
    int aspect = 0;          // 0 = 4:3, 1 = stretch to fill, 2 = widescreen (extra scenery at the sides)
    bool smooth = false;
    bool fullscreen = false;
    int scale = 4;           // window size multiplier (PC)
    int language = 0;        // 0 = English, 1 = Japanese
    bool perf = false;       // performance readout in the corner
    bool cheat_hp = false;       // infinite health
    bool cheat_hearts = false;   // infinite hearts
    int subweapon = 0;           // index into the Cheats page subweapon list
    int kb[BIND_COUNT];      // keyboard code per action (-1 = none)
    int pad[BIND_COUNT];     // gamepad button code per action (-1 = none)
    MenuSettings() { for (int i = 0; i < BIND_COUNT; i++) { kb[i] = -1; pad[i] = -1; } }

    std::string Serialize() const;
    void Parse(const std::string& text);
};

// Implemented by each front end.
struct MenuHost
{
    virtual ~MenuHost() {}
    virtual bool HasKeyboard() = 0;
    virtual bool HasWindow() = 0;                 // window size / fullscreen options
    virtual std::string KeyName(int code) = 0;
    virtual std::string PadName(int code) = 0;
    virtual std::string SlotInfo(int slot) = 0;   // "Empty" or a save time
    virtual void DefaultBindings(MenuSettings& s) = 0;
};

class Menu
{
public:
    static const int W = 320, H = 240;

    Menu(MenuHost* host, MenuSettings* settings) : m_host(host), m_s(settings) {}

    bool IsOpen() const { return m_open; }
    void Open();
    MenuResult Close();

    MenuResult Nav(MenuNav n);

    // Rebinding: while capturing, the front end sends the next raw key or button here.
    bool IsCapturing() const { return m_capture >= 0; }
    bool CapturingKeyboard() const { return m_capture_col == 0; }
    MenuResult Captured(int code);          // -1 cancels
    void Tick();                            // once per frame: timeouts and toasts

    void Toast(const std::string& text);
    bool Visible() const { return m_open || m_toast_frames > 0 || (m_s->perf && !m_hud.empty()); }

    // Performance readout text (shown in the top left corner when settings.perf is on).
    void SetHud(const std::string& text) { m_hud = text; }

    // Draws the overlay. Pixels are RGBA bytes, W*H*4, fully rewritten each call.
    void Draw(uint8_t* rgba);

private:
    enum Page { PAGE_MAIN, PAGE_CONTROLS, PAGE_OPTIONS, PAGE_CHEATS };
    int ItemCount() const;
    MenuResult Activate(int dir);
    void Text(uint8_t* p, int x, int y, const std::string& s, uint32_t rgba);
    void Fill(uint8_t* p, int x, int y, int w, int h, uint32_t rgba);
    std::string BindName(int action, int col);

    MenuHost* m_host;
    MenuSettings* m_s;
    bool m_open = false;
    Page m_page = PAGE_MAIN;
    int m_sel = 0;
    int m_col = 0;              // controls page: 0 keyboard, 1 gamepad
    int m_capture = -1;         // action being rebound
    int m_capture_col = 0;
    int m_capture_frames = 0;
    std::string m_toast;
    std::string m_hud;
    int m_toast_frames = 0;
    int m_lang_pending = -1;    // language change waiting for confirmation
};

const char* BindActionName(int a);

// Cheats (Richter): game RAM is bank F8 (work RAM). The player status block is only
// filled during gameplay; outside it everything there is zero.
//   F8:0093 subweapon  F8:0094 hearts (BCD)  F8:0098 health  F8:0099 max health
enum { SUBWEAPON_COUNT = 7 };
const char* SubweaponName(int i);     // display name for Cheats page index i
uint8_t SubweaponId(int i);           // game's value for F8:0093
// Applies cheats to work RAM once per frame. give_subweapon: index to hand over now
// (-1 for none); returns true when it was applied (only during gameplay).
bool ApplyCheats(uint8_t* wram, const MenuSettings& s, int give_subweapon);

// Copies a game save file to <its folder>/backups/<name>-YYYYMMDD.sav, once a day,
// skipping an empty (all zero) file, so a bad write can always be rolled back.
void BackupSaveFile(const std::string& path);

#endif
