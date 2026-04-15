#include "ui/app.hpp"
#include "ui/renderer.hpp"
#include "ui/screens/screen.hpp"
#include "ui/screens/login_screen.hpp"
#include "ui/screens/platform_screen.hpp"
#include "ui/screens/game_screen.hpp"
#include "ui/screens/collection_screen.hpp"
#include "ui/screens/detail_screen.hpp"
#include "ui/screens/browse_screen.hpp"
#include "config.hpp"

#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <curl/curl.h>
#include <array>
#include <cstring>

// ---------------------------------------------------------------------------
// Nintendo Switch controller → SDL_Keycode mapping
// ---------------------------------------------------------------------------
// On the Switch SDL2 port, controller buttons arrive as SDL_JOYBUTTONDOWN
// events with the following indices (combined / handheld controller):
//   0 = A    1 = B    2 = X    3 = Y
//   4 = L-Stick click  5 = R-Stick click
//   6 = L    7 = R
//   8 = ZL   9 = ZR
//  10 = Plus (+)  11 = Minus (-)
//  12 = D-Left  13 = D-Up  14 = D-Right  15 = D-Down
// ---------------------------------------------------------------------------
static SDL_Keycode joyButtonToKey(Uint8 button) {
    switch (button) {
    case 0:  return SDLK_RETURN;   // A  → confirm / select
    case 1:  return SDLK_b;        // B  → back / cancel
    case 2:  return SDLK_x;        // X  → save / download
    case 3:  return SDLK_y;        // Y  → search / filter
    case 6:  return SDLK_PAGEUP;   // L  → page up
    case 7:  return SDLK_PAGEDOWN; // R  → page down
    case 12: return SDLK_LEFT;     // D-Left
    case 13: return SDLK_UP;       // D-Up
    case 14: return SDLK_RIGHT;    // D-Right
    case 15: return SDLK_DOWN;     // D-Down
    default: return SDLK_UNKNOWN;
    }
}

// Translate joystick button presses into the SDL_KEYDOWN events that every
// Screen already handles.  Returns true when *out* has been filled in.
static bool translateJoyButton(const SDL_Event& in, SDL_Event& out) {
    if (in.type != SDL_JOYBUTTONDOWN) return false;

    // Plus button (10) → generate an SDL_QUIT so the app exits cleanly.
    if (in.jbutton.button == 10) {
        std::memset(&out, 0, sizeof(out));
        out.type = SDL_QUIT;
        return true;
    }

    SDL_Keycode key = joyButtonToKey(in.jbutton.button);
    if (key == SDLK_UNKNOWN) return false;

    std::memset(&out, 0, sizeof(out));
    out.type           = SDL_KEYDOWN;
    out.key.keysym.sym = key;
    return true;
}

// Translate left-stick axis motion into D-pad–style key events.
// A simple threshold + edge-detect approach avoids flooding the screen with
// repeated events while the stick is held.
static constexpr Sint16 STICK_DEADZONE = 16000;

static bool translateJoyAxis(const SDL_Event& in, SDL_Event& out,
                             bool& stickUp, bool& stickDown,
                             bool& stickLeft, bool& stickRight) {
    if (in.type != SDL_JOYAXISMOTION) return false;

    Sint16 val = in.jaxis.value;
    SDL_Keycode key = SDLK_UNKNOWN;

    if (in.jaxis.axis == 1) { // Y-axis
        bool wantUp   = (val < -STICK_DEADZONE);
        bool wantDown = (val >  STICK_DEADZONE);

        if (wantUp && !stickUp)        key = SDLK_UP;
        else if (wantDown && !stickDown) key = SDLK_DOWN;

        stickUp   = wantUp;
        stickDown = wantDown;
    } else if (in.jaxis.axis == 0) { // X-axis
        bool wantLeft  = (val < -STICK_DEADZONE);
        bool wantRight = (val >  STICK_DEADZONE);

        if (wantLeft && !stickLeft)       key = SDLK_LEFT;
        else if (wantRight && !stickRight) key = SDLK_RIGHT;

        stickLeft  = wantLeft;
        stickRight = wantRight;
    }

    if (key == SDLK_UNKNOWN) return false;

    std::memset(&out, 0, sizeof(out));
    out.type           = SDL_KEYDOWN;
    out.key.keysym.sym = key;
    return true;
}

// ---------------------------------------------------------------------------
// UI layout constants shared across the app
// ---------------------------------------------------------------------------
static constexpr int HEADER_H  = 60;
static constexpr int STATUS_H  = 44;

class MainMenuScreen : public Screen {
public:
    MainMenuScreen(Renderer& renderer, NavigateFn navigate,
                   bool hasConfig, bool loggedIn, const std::string& loginError)
        : Screen(renderer, std::move(navigate))
        , m_hasConfig(hasConfig)
        , m_loggedIn(loggedIn)
        , m_loginError(loginError)
        , m_selected(0)
    {}

    void onEnter() override { m_selected = 0; }

    bool update(const SDL_Event& event) override {
        if (event.type != SDL_KEYDOWN) return true;
        switch (event.key.keysym.sym) {
        case SDLK_UP:
            if (m_selected > 0) --m_selected;
            break;
        case SDLK_DOWN:
            if (m_selected < static_cast<int>(m_items.size()) - 1) ++m_selected;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            select();
            break;
        default: break;
        }
        return true;
    }

    void render() override {
        auto& R = m_renderer;
        R.fillRect(0, 0, SCREEN_W, SCREEN_H, Color::Background);
        R.drawHeader("RomM2Switch");

        if (!m_hasConfig) {
            R.drawTextCentered("Server not configured.",
                               0, SCREEN_H / 2 - 60, SCREEN_W,
                               Color::Error, R.fontLarge());
            R.drawTextCentered("Please open Settings to enter your RomM server details.",
                               0, SCREEN_H / 2, SCREEN_W,
                               Color::TextDim, R.fontSmall());
        } else if (m_loggedIn) {
            R.drawTextCentered("Connected to RomM",
                               0, HEADER_H + 20, SCREEN_W,
                               Color::Success, R.fontSmall());
        } else if (!m_loginError.empty()) {
            R.drawTextCentered("Login failed: " + m_loginError,
                               0, HEADER_H + 20, SCREEN_W,
                               Color::Error, R.fontSmall());
        }

        int startY = HEADER_H + 80;
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            bool sel = (i == m_selected);
            int  y   = startY + i * 90;

            int  bw  = 400, bh = 60;
            int  bx  = (SCREEN_W - bw) / 2;
            R.fillRect(bx, y, bw, bh, sel ? Color::CardHover : Color::Card);
            R.drawRect(bx, y, bw, bh, sel ? Color::CardHover : Color::Separator);
            R.drawTextCentered(m_items[static_cast<size_t>(i)], bx, y + (bh - 26) / 2,
                               bw, sel ? Color::TextWhite : Color::Text,
                               R.fontLarge());
        }

        R.drawStatusBar("Up/Down Navigate  A Select");
    }

private:
    bool m_hasConfig;
    bool m_loggedIn;
    std::string m_loginError;
    int  m_selected;
    std::array<const char*, 2> m_items = {"Browse", "Settings"};

    void select() {
        switch (m_selected) {
        case 0: navigateTo("browse",   0); break;
        case 1: navigateTo("settings", 0); break;
        }
    }
};

// ---------------------------------------------------------------------------
// App implementation
// ---------------------------------------------------------------------------

App::App() = default;
App::~App() { cleanup(); }

bool App::init() {
    // libnx services
    socketInitializeDefault();
    plInitialize(PlServiceType_User);

    // Initialize libcurl once for the process lifetime
    curl_global_init(CURL_GLOBAL_ALL);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) return false;
    if (TTF_Init() < 0) { SDL_Quit(); return false; }
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);

    m_window = SDL_CreateWindow("RomM2Switch",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
    if (!m_window) return false;

    m_sdlRend = SDL_CreateRenderer(m_window, -1,
                                   SDL_RENDERER_ACCELERATED |
                                   SDL_RENDERER_PRESENTVSYNC);
    if (!m_sdlRend) return false;

    // Use the Switch system font (NintendoExt003 / Standard)
    PlFontData fontData;
    if (R_SUCCEEDED(plGetSharedFontByType(&fontData, PlSharedFontType_Standard))) {
        SDL_RWops* rw = SDL_RWFromMem(fontData.address, static_cast<int>(fontData.size));
        m_fontLg = TTF_OpenFontRW(rw, 0, 32);
        SDL_RWops* rw2 = SDL_RWFromMem(fontData.address, static_cast<int>(fontData.size));
        m_fontMd = TTF_OpenFontRW(rw2, 0, 26);
        SDL_RWops* rw3 = SDL_RWFromMem(fontData.address, static_cast<int>(fontData.size));
        m_fontSm = TTF_OpenFontRW(rw3, 0, 20);
    }

    if (!m_fontLg || !m_fontMd || !m_fontSm) return false;

    // Open the first available joystick (the combined controller on Switch)
    if (SDL_NumJoysticks() > 0)
        m_joystick = SDL_JoystickOpen(0);

    m_renderer = std::make_unique<Renderer>(m_sdlRend, m_fontLg, m_fontMd, m_fontSm);
    m_config   = romm::loadConfig();

    if (m_config.isConfigured()) {
        m_client = std::make_unique<romm::RommClient>(m_config);
        // Attempt login and track result for the UI
        m_loggedIn = m_client->login(m_loginError);
    }

    return true;
}

void App::cleanup() {
    m_current.reset();
    m_savedBrowse.reset();
    m_client.reset();
    m_renderer.reset();

    if (m_joystick) SDL_JoystickClose(m_joystick);
    if (m_fontLg)  TTF_CloseFont(m_fontLg);
    if (m_fontMd)  TTF_CloseFont(m_fontMd);
    if (m_fontSm)  TTF_CloseFont(m_fontSm);
    if (m_sdlRend) SDL_DestroyRenderer(m_sdlRend);
    if (m_window)  SDL_DestroyWindow(m_window);

    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    curl_global_cleanup();
    plExit();
    socketExit();
}

void App::navigateTo(const std::string& name, int id) {
    // When navigating to "detail" from the browse screen, preserve it
    // so the user returns to the same platform/collection selection.
    if (name == "detail" && m_current) {
        m_savedBrowse = std::move(m_current);
    }

    // When going back, restore the saved browse screen instead of
    // creating a brand-new one (which would reset the selection).
    if (name == "back" && m_savedBrowse) {
        m_current = std::move(m_savedBrowse);
        return;
    }

    // Discard stale saved browse for any other navigation target.
    if (name != "detail") {
        m_savedBrowse.reset();
    }

    auto next = makeScreen(name, id);
    if (next) {
        m_current = std::move(next);
        m_current->onEnter();
    }
}

std::unique_ptr<Screen> App::makeScreen(const std::string& name, int id) {
    auto nav = [this](const std::string& n, int i) { navigateTo(n, i); };

    if (name == "main") {
        return std::make_unique<MainMenuScreen>(
            *m_renderer, nav, m_config.isConfigured(),
            m_loggedIn, m_loginError);
    }

    if (name == "settings") {
        return std::make_unique<LoginScreen>(
            *m_renderer, nav, m_config,
            [this](const romm::Config& cfg) {
                m_config = cfg;
                romm::saveConfig(m_config);
                // Re-create client with new config and test login
                m_client = std::make_unique<romm::RommClient>(m_config);
                m_loginError.clear();
                m_loggedIn = m_client->login(m_loginError);
            });
    }

    if (name == "browse") {
        if (!m_client) return makeScreen("main", 0);
        return std::make_unique<BrowseScreen>(*m_renderer, nav, *m_client);
    }

    if (name == "platforms") {
        if (!m_client) return makeScreen("main", 0);
        return std::make_unique<PlatformScreen>(*m_renderer, nav, *m_client);
    }

    if (name == "games_platform") {
        if (!m_client) return makeScreen("main", 0);
        return std::make_unique<GameScreen>(
            *m_renderer, nav, *m_client, id, GameListMode::Platform);
    }

    if (name == "games_collection") {
        if (!m_client) return makeScreen("main", 0);
        return std::make_unique<GameScreen>(
            *m_renderer, nav, *m_client, id, GameListMode::Collection);
    }

    if (name == "collections") {
        if (!m_client) return makeScreen("main", 0);
        return std::make_unique<CollectionScreen>(*m_renderer, nav, *m_client);
    }

    if (name == "detail") {
        if (!m_client) return makeScreen("main", 0);
        return std::make_unique<DetailScreen>(
            *m_renderer, nav, *m_client, m_config, id);
    }

    if (name == "back") {
        // Navigate back to the browse screen; in a more complex app
        // this could be a proper navigation stack.
        return makeScreen("browse", 0);
    }

    return nullptr;
}

void App::run() {
    if (!init()) return;

    // Start on main menu
    navigateTo("main", 0);

    bool running = true;
    bool stickUp = false, stickDown = false;
    bool stickLeft = false, stickRight = false;
    while (running && appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Translate controller button / stick events to keyboard events
            // so that every Screen's update() works without modification.
            SDL_Event translated;
            if (translateJoyButton(event, translated) ||
                translateJoyAxis(event, translated, stickUp, stickDown,
                                 stickLeft, stickRight)) {
                event = translated;
            }

            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }

            if (m_current && !m_current->update(event)) {
                running = false;
            }
        }

        if (!m_current) break;

        SDL_SetRenderDrawColor(m_sdlRend, 13, 17, 23, 255);
        SDL_RenderClear(m_sdlRend);

        m_current->render();

        SDL_RenderPresent(m_sdlRend);
    }
}
