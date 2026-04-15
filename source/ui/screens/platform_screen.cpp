#include "ui/screens/platform_screen.hpp"
#include "ui/renderer.hpp"

PlatformScreen::PlatformScreen(Renderer& renderer, NavigateFn navigate,
                                romm::RommClient& client)
    : Screen(renderer, std::move(navigate))
    , m_client(client)
{}

void PlatformScreen::onEnter() {
    m_loading  = true;
    m_error.clear();
    m_selected  = 0;
    m_scrollOff = 0;
    loadData();
}

void PlatformScreen::loadData() {
    m_platforms = m_client.getPlatforms(m_error);
    m_loading   = false;
}

void PlatformScreen::clampScroll() {
    int maxSel = static_cast<int>(m_platforms.size()) - 1;
    if (m_selected < 0) m_selected = 0;
    if (m_selected > maxSel) m_selected = maxSel;

    if (m_selected < m_scrollOff)
        m_scrollOff = m_selected;
    if (m_selected >= m_scrollOff + VISIBLE_ROWS)
        m_scrollOff = m_selected - VISIBLE_ROWS + 1;
}

bool PlatformScreen::update(const SDL_Event& event) {
    if (m_loading) return true;

    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
        case SDLK_UP:
            --m_selected;
            clampScroll();
            break;
        case SDLK_DOWN:
            ++m_selected;
            clampScroll();
            break;
        case SDLK_PAGEUP:
            m_selected -= VISIBLE_ROWS;
            clampScroll();
            break;
        case SDLK_PAGEDOWN:
            m_selected += VISIBLE_ROWS;
            clampScroll();
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (!m_platforms.empty())
                navigateTo("games_platform", m_platforms[static_cast<size_t>(m_selected)].id);
            break;
        case SDLK_b:
            navigateTo("main", 0);
            break;
        default: break;
        }
    }
    return true;
}

void PlatformScreen::render() {
    auto& R = m_renderer;
    R.fillRect(0, 0, SCREEN_W, SCREEN_H, Color::Background);
    R.drawHeader("Platforms");

    if (m_loading) {
        R.drawLoadingOverlay("Loading platforms...");
        R.drawStatusBar("");
        return;
    }

    if (!m_error.empty()) {
        R.drawErrorScreen(m_error);
        R.drawStatusBar("B Back");
        return;
    }

    if (m_platforms.empty()) {
        R.drawTextCentered("No platforms found.", 0, SCREEN_H / 2 - 20,
                           SCREEN_W, Color::TextDim, R.fontLarge());
        R.drawStatusBar("B Back");
        return;
    }

    // Draw list items
    int end = std::min(m_scrollOff + VISIBLE_ROWS,
                       static_cast<int>(m_platforms.size()));
    for (int i = m_scrollOff; i < end; ++i) {
        const auto& p  = m_platforms[static_cast<size_t>(i)];
        int y          = CONTENT_Y + (i - m_scrollOff) * ITEM_H;
        bool selected  = (i == m_selected);

        R.fillRect(10, y + 6, SCREEN_W - 20, ITEM_H - 12,
                   selected ? Color::Card : Color::Header);
        R.drawRect(10, y + 6, SCREEN_W - 20, ITEM_H - 12,
                   selected ? Color::CardHover : Color::Separator);
        if (selected) {
            R.fillRect(10, y + 6, 4, ITEM_H - 12, Color::CardHover);
        }

        // Platform name
        R.drawText(p.name, 34, y + (ITEM_H - 26) / 2,
                   Color::Text);

        // ROM count (right-aligned)
        std::string countStr = std::to_string(p.romCount) + " ROMs";
        int cw = R.textWidth(countStr, R.fontSmall());
        R.drawText(countStr, SCREEN_W - cw - 30, y + (ITEM_H - 20) / 2,
                   Color::TextDim, R.fontSmall());

        // Separator line
        R.fillRect(30, y + ITEM_H - 1, SCREEN_W - 60, 1, Color::Separator);
    }

    // Scroll indicator
    if (static_cast<int>(m_platforms.size()) > VISIBLE_ROWS) {
        float ratio   = static_cast<float>(VISIBLE_ROWS) / m_platforms.size();
        float offset  = static_cast<float>(m_scrollOff) / m_platforms.size();
        int   barH    = static_cast<int>(CONTENT_H * ratio);
        int   barY    = CONTENT_Y + static_cast<int>(CONTENT_H * offset);
        R.fillRect(SCREEN_W - 6, barY, 4, barH, Color::CardHover);
    }

    R.drawStatusBar("Up/Down Navigate  A Open Platform  B Back");
}
