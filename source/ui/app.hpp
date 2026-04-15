#pragma once

#include "ui/renderer.hpp"
#include "config.hpp"
#include "api/romm_client.hpp"

#include <memory>
#include <string>

class Screen;

class App {
public:
    App();
    ~App();

    // Run the main loop. Returns when the user exits.
    void run();

private:
    // SDL / window
    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_sdlRend  = nullptr;
    SDL_Joystick* m_joystick = nullptr;
    TTF_Font*     m_fontLg   = nullptr;
    TTF_Font*     m_fontMd   = nullptr;
    TTF_Font*     m_fontSm   = nullptr;

    std::unique_ptr<Renderer>    m_renderer;
    std::unique_ptr<Screen>      m_current;

    romm::Config                 m_config;
    std::unique_ptr<romm::RommClient> m_client;
    bool                         m_loggedIn = false;
    std::string                  m_loginError;

    // Called by screens to switch to a new screen by name + optional id
    void navigateTo(const std::string& name, int id);

    // Create a screen by name
    std::unique_ptr<Screen> makeScreen(const std::string& name, int id);

    bool init();
    void cleanup();
};
