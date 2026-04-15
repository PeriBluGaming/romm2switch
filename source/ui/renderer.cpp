#include "ui/renderer.hpp"
#include <SDL2/SDL_image.h>
#include <cstring>
#include <algorithm>

static constexpr int HEADER_H    = 60;
static constexpr int STATUS_H    = 44;
static constexpr int CONTENT_Y   = HEADER_H;
static constexpr int CONTENT_H   = SCREEN_H - HEADER_H - STATUS_H;

Renderer::Renderer(SDL_Renderer* sdlRenderer, TTF_Font* fontLarge,
                   TTF_Font* fontMedium, TTF_Font* fontSmall)
    : m_renderer(sdlRenderer)
    , m_fontLarge(fontLarge)
    , m_fontMedium(fontMedium)
    , m_fontSmall(fontSmall)
{}

void Renderer::fillRect(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, c.a);
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(m_renderer, &r);
}

void Renderer::drawRect(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, c.a);
    SDL_Rect r{x, y, w, h};
    SDL_RenderDrawRect(m_renderer, &r);
}

void Renderer::drawText(const std::string& text, int x, int y,
                        SDL_Color color, TTF_Font* font) {
    if (!font) font = m_fontMedium;
    if (text.empty()) return;

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

void Renderer::drawTextCentered(const std::string& text, int x, int y,
                                 int w, SDL_Color color, TTF_Font* font) {
    if (!font) font = m_fontMedium;
    if (text.empty()) return;
    int tw = 0, th = 0;
    TTF_SizeUTF8(font, text.c_str(), &tw, &th);
    drawText(text, x + (w - tw) / 2, y, color, font);
}

void Renderer::drawHeader(const std::string& title) {
    fillRect(0, 0, SCREEN_W, HEADER_H, Color::Header);
    fillRect(0, HEADER_H - 1, SCREEN_W, 1, Color::Separator);
    drawText(title, 28, (HEADER_H - 32) / 2, Color::TextWhite, m_fontLarge);
}

void Renderer::drawStatusBar(const std::string& hint) {
    fillRect(0, SCREEN_H - STATUS_H, SCREEN_W, STATUS_H, Color::StatusBar);
    fillRect(0, SCREEN_H - STATUS_H, SCREEN_W, 1, Color::Separator);
    drawText(hint, 24, SCREEN_H - STATUS_H + (STATUS_H - 20) / 2,
             Color::TextDim, m_fontSmall);
}

void Renderer::drawProgressBar(int x, int y, int w, int h, float progress) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    fillRect(x, y, w, h, Color::ProgressBg);
    int filled = static_cast<int>(w * progress);
    if (filled > 0) fillRect(x, y, filled, h, Color::ProgressFg);
    drawRect(x, y, w, h, Color::CardHover);
}

void Renderer::drawLoadingOverlay(const std::string& message) {
    // Semi-transparent overlay
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 7, 10, 14, 190);
    SDL_Rect overlay{0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(m_renderer, &overlay);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Centered box
    int bw = 520, bh = 130;
    int bx = (SCREEN_W - bw) / 2, by = (SCREEN_H - bh) / 2;
    fillRect(bx, by, bw, bh, Color::Header);
    drawRect(bx, by, bw, bh, Color::Separator);
    drawTextCentered("Loading...", bx, by + 24, bw, Color::TextWhite, m_fontLarge);
    if (!message.empty()) {
        drawTextCentered(message, bx, by + 78, bw, Color::TextDim, m_fontSmall);
    }
}

void Renderer::drawErrorScreen(const std::string& message) {
    fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, Color::Background);
    int bw = 760, bh = 170;
    int bx = (SCREEN_W - bw) / 2, by = CONTENT_Y + (CONTENT_H - bh) / 2;
    fillRect(bx, by, bw, bh, Color::Header);
    drawRect(bx, by, bw, bh, Color::Error);
    drawTextCentered("Error", bx, by + 20, bw, Color::Error, m_fontLarge);
    drawTextCentered(message, bx + 24, by + 86, bw - 48, Color::TextDim, m_fontSmall);
}

int Renderer::textWidth(const std::string& text, TTF_Font* font) {
    if (!font) font = m_fontMedium;
    int tw = 0;
    TTF_SizeUTF8(font, text.c_str(), &tw, nullptr);
    return tw;
}

SDL_Texture* Renderer::loadTextureFromMemory(const std::vector<uint8_t>& data) {
    if (data.empty()) return nullptr;

    SDL_RWops* rw = SDL_RWFromConstMem(data.data(), static_cast<int>(data.size()));
    if (!rw) return nullptr;

    SDL_Surface* surface = IMG_Load_RW(rw, 1); // 1 = auto-close RW
    if (!surface) return nullptr;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

void Renderer::drawTexture(SDL_Texture* texture, int x, int y, int w, int h) {
    if (!texture) return;
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
}

void Renderer::drawTextureFit(SDL_Texture* texture, int x, int y, int w, int h) {
    if (!texture) return;
    int tw = 0, th = 0;
    SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
    if (tw <= 0 || th <= 0) return;

    float scaleX = static_cast<float>(w) / tw;
    float scaleY = static_cast<float>(h) / th;
    float scale  = std::min(scaleX, scaleY);

    int dw = static_cast<int>(tw * scale);
    int dh = static_cast<int>(th * scale);
    int dx = x + (w - dw) / 2;
    int dy = y + (h - dh) / 2;

    SDL_Rect dst = {dx, dy, dw, dh};
    SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
}
