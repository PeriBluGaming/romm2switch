#include "ui/app.hpp"
#include "ui/renderer.hpp"
#include "ui/screens/screen.hpp"
#include "ui/screens/login_screen.hpp"
#include "ui/screens/platform_screen.hpp"
#include "ui/screens/game_screen.hpp"
#include "ui/screens/collection_screen.hpp"
#include "ui/screens/detail_screen.hpp"
#include "config.hpp"

#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <array>

// ---------------------------------------------------------------------------
// UI layout constants shared across the app
// ---------------------------------------------------------------------------
static constexpr int HEADER_H  = 60;
static constexpr int STATUS_H  = 44;

class MainMenuScreen : public Screen {
public:
    MainMenuScreen(Renderer& renderer, NavigateFn navigate,
                   bool hasConfig)
        : Screen(renderer, std::move(navigate))
        , m_hasConfig(hasConfig)
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

        R.drawStatusBar("\xe2\x86\x91\xe2\x86\x93 Navigate  \xe2\x8f\xb5 Select");
    }

private:
    bool m_hasConfig;
    int  m_selected;
    std::array<const char*, 3> m_items = {"Platforms", "Collections", "Settings"};

    void select() {
        switch (m_selected) {
        case 0: navigateTo("platforms",   0); break;
        case 1: navigateTo("collections", 0); break;
        case 2: navigateTo("settings",    0); break;
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) return false;
    if (TTF_Init() < 0) { SDL_Quit(); return false; }

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

    m_renderer = std::make_unique<Renderer>(m_sdlRend, m_fontLg, m_fontMd, m_fontSm);
    m_config   = romm::loadConfig();

    if (m_config.isConfigured()) {
        m_client = std::make_unique<romm::RommClient>(m_config);
        // Attempt login; ignore failure here (user can fix in settings)
        std::string err;
        m_client->login(err);
    }

    return true;
}

void App::cleanup() {
    m_current.reset();
    m_client.reset();
    m_renderer.reset();

    if (m_fontLg)  TTF_CloseFont(m_fontLg);
    if (m_fontMd)  TTF_CloseFont(m_fontMd);
    if (m_fontSm)  TTF_CloseFont(m_fontSm);
    if (m_sdlRend) SDL_DestroyRenderer(m_sdlRend);
    if (m_window)  SDL_DestroyWindow(m_window);

    TTF_Quit();
    SDL_Quit();
    plExit();
    socketExit();
}

void App::navigateTo(const std::string& name, int id) {
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
            *m_renderer, nav, m_config.isConfigured());
    }

    if (name == "settings") {
        return std::make_unique<LoginScreen>(
            *m_renderer, nav, m_config,
            [this](const romm::Config& cfg) {
                m_config = cfg;
                romm::saveConfig(m_config);
                // Re-create client with new config and login
                m_client = std::make_unique<romm::RommClient>(m_config);
                std::string err;
                m_client->login(err);
            });
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
        // Navigate back to main; in a more complex app this could be a stack
        return makeScreen("main", 0);
    }

    return nullptr;
}

void App::run() {
    if (!init()) return;

    // Start on main menu
    navigateTo("main", 0);

    bool running = true;
    while (running && appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }
            // Map joystick / gamepad to keyboard events for uniform handling
            // (SDL2 on Switch maps buttons to keyboard events automatically
            //  via the SDL Switch port)

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
