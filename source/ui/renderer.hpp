#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// Screen dimensions (Switch native resolution in docked mode)
// ---------------------------------------------------------------------------
static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

// ---------------------------------------------------------------------------
// UI color palette
// ---------------------------------------------------------------------------
namespace Color {
    static constexpr SDL_Color Background  = {11,  15,  20,  255}; // #0b0f14
    static constexpr SDL_Color Header      = {17,  23,  31,  255}; // #11171f
    static constexpr SDL_Color HeaderText  = {255, 255, 255, 255};
    static constexpr SDL_Color Card        = {20,  28,  38,  255}; // #141c26
    static constexpr SDL_Color CardHover   = {56,  122, 245, 255}; // #387af5
    static constexpr SDL_Color Text        = {230, 236, 243, 255}; // #e6ecf3
    static constexpr SDL_Color TextDim     = {155, 168, 184, 255}; // #9ba8b8
    static constexpr SDL_Color TextWhite   = {255, 255, 255, 255};
    static constexpr SDL_Color Separator   = {41,  53,  68,  255}; // #293544
    static constexpr SDL_Color StatusBar   = {17,  23,  31,  255};
    static constexpr SDL_Color ProgressBg  = {36,  47,  62,  255};
    static constexpr SDL_Color ProgressFg  = {72,  156, 255, 255};
    static constexpr SDL_Color Error       = {248,  81,  73, 255}; // #f85149
    static constexpr SDL_Color Success     = {80,  201, 117, 255}; // #50c975
    static constexpr SDL_Color SidebarBg   = {16,  22,  30,  255};
    static constexpr SDL_Color TabActive   = {56,  122, 245, 255};
    static constexpr SDL_Color TabInactive = {28,  38,  50,  255};
}

// ---------------------------------------------------------------------------
// Renderer helpers
// ---------------------------------------------------------------------------
class Renderer {
public:
    Renderer(SDL_Renderer* sdlRenderer, TTF_Font* fontLarge,
             TTF_Font* fontMedium, TTF_Font* fontSmall);

    // Draw a filled rectangle
    void fillRect(int x, int y, int w, int h, SDL_Color color);

    // Draw a rectangle outline
    void drawRect(int x, int y, int w, int h, SDL_Color color);

    // Draw text (left-aligned)
    void drawText(const std::string& text, int x, int y,
                  SDL_Color color, TTF_Font* font = nullptr);

    // Draw text centered within a rectangle
    void drawTextCentered(const std::string& text, int x, int y, int w,
                          SDL_Color color, TTF_Font* font = nullptr);

    // Draw the standard app header bar
    void drawHeader(const std::string& title);

    // Draw the status/help bar at the bottom
    void drawStatusBar(const std::string& hint);

    // Draw a progress bar
    void drawProgressBar(int x, int y, int w, int h, float progress);

    // Draw a loading overlay
    void drawLoadingOverlay(const std::string& message);

    // Draw an error message centered on screen
    void drawErrorScreen(const std::string& message);

    // Measure text width in pixels
    int textWidth(const std::string& text, TTF_Font* font = nullptr);

    // --- Texture / image support ---

    // Load an SDL_Texture from raw image bytes (JPEG/PNG).
    // Caller owns the returned texture and must call SDL_DestroyTexture().
    // Returns nullptr on failure.
    SDL_Texture* loadTextureFromMemory(const std::vector<uint8_t>& data);

    // Draw a texture stretched to fill the given rectangle.
    void drawTexture(SDL_Texture* texture, int x, int y, int w, int h);

    // Draw a texture scaled to fit within the given rectangle while
    // preserving aspect ratio (centered, with letterboxing).
    void drawTextureFit(SDL_Texture* texture, int x, int y, int w, int h);

    TTF_Font* fontLarge()  { return m_fontLarge; }
    TTF_Font* fontMedium() { return m_fontMedium; }
    TTF_Font* fontSmall()  { return m_fontSmall; }

    SDL_Renderer* sdlRenderer() { return m_renderer; }

private:
    SDL_Renderer* m_renderer;
    TTF_Font*     m_fontLarge;
    TTF_Font*     m_fontMedium;
    TTF_Font*     m_fontSmall;
};
