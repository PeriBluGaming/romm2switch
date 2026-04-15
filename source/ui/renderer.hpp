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
    static constexpr SDL_Color Background  = {13,  17,  23,  255}; // #0d1117
    static constexpr SDL_Color Header      = {228,  0,  15,  255}; // #e4000f (Nintendo red)
    static constexpr SDL_Color HeaderText  = {255, 255, 255, 255};
    static constexpr SDL_Color Card        = {22,  27,  34,  255}; // #161b22
    static constexpr SDL_Color CardHover   = {31,  111, 235, 255}; // #1f6feb
    static constexpr SDL_Color Text        = {201, 209, 217, 255}; // #c9d1d9
    static constexpr SDL_Color TextDim     = {139, 148, 158, 255}; // #8b949e
    static constexpr SDL_Color TextWhite   = {255, 255, 255, 255};
    static constexpr SDL_Color Separator   = {48,  54,  61,  255}; // #30363d
    static constexpr SDL_Color StatusBar   = {22,  27,  34,  255};
    static constexpr SDL_Color ProgressBg  = {48,  54,  61,  255};
    static constexpr SDL_Color ProgressFg  = {35,  134,  54, 255}; // #238636 green
    static constexpr SDL_Color Error       = {248,  81,  73, 255}; // #f85149
    static constexpr SDL_Color Success     = {63,  185, 80,  255}; // #3fb950
    static constexpr SDL_Color SidebarBg   = {17,  21,  28,  255}; // slightly lighter than Background
    static constexpr SDL_Color TabActive   = {31,  111, 235, 255}; // same as CardHover
    static constexpr SDL_Color TabInactive = {30,  35,  44,  255};
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
