#pragma once

#include <SDL2/SDL.h>
#include <functional>
#include <string>

class Renderer;
namespace romm { class RommClient; }

// ---------------------------------------------------------------------------
// Screen — abstract base class for all UI screens
// ---------------------------------------------------------------------------
class Screen {
public:
    // Type alias for a screen-change callback: provides the new screen name
    // and optional context (e.g., selected item ID).
    using NavigateFn = std::function<void(const std::string& screenName, int contextId)>;

    explicit Screen(Renderer& renderer, NavigateFn navigate)
        : m_renderer(renderer), m_navigate(std::move(navigate)) {}

    virtual ~Screen() = default;

    // Called when this screen becomes active.
    virtual void onEnter() {}

    // Called each frame. Returns false to request app exit.
    virtual bool update(const SDL_Event& event) { (void)event; return true; }

    // Called each frame to render the screen.
    virtual void render() = 0;

    // Optional hook for screens that need to stop background work before the
    // app swaps shared client state.
    virtual void pauseForClientSwap() {}

    // Optional hook for screens that want updated session/login state after a
    // settings save recreated the RomM client.
    virtual void setSessionState(romm::RommClient* /*client*/,
                                 bool /*hasConfig*/,
                                 bool /*loggedIn*/,
                                 const std::string& /*loginError*/) {}

protected:
    Renderer&  m_renderer;
    NavigateFn m_navigate;

    // Helper: navigate to a named screen with optional context id
    void navigateTo(const std::string& name, int id = 0) {
        m_navigate(name, id);
    }
};
