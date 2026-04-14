#include "ui/screens/collection_screen.hpp"
#include "ui/renderer.hpp"

CollectionScreen::CollectionScreen(Renderer& renderer, NavigateFn navigate,
                                   romm::RommClient& client)
    : Screen(renderer, std::move(navigate))
    , m_client(client)
{}

void CollectionScreen::onEnter() {
    m_loading    = true;
    m_error.clear();
    m_selected   = 0;
    m_scrollOff  = 0;
    loadData();
}

void CollectionScreen::loadData() {
    m_collections = m_client.getCollections(m_error);
    m_loading     = false;
}

void CollectionScreen::clampScroll() {
    int maxSel = static_cast<int>(m_collections.size()) - 1;
    if (m_selected < 0) m_selected = 0;
    if (m_selected > maxSel) m_selected = maxSel;

    if (m_selected < m_scrollOff)
        m_scrollOff = m_selected;
    if (m_selected >= m_scrollOff + VISIBLE_ROWS)
        m_scrollOff = m_selected - VISIBLE_ROWS + 1;
}

bool CollectionScreen::update(const SDL_Event& event) {
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
            if (!m_collections.empty())
                navigateTo("games_collection",
                           m_collections[static_cast<size_t>(m_selected)].id);
            break;
        case SDLK_B:
            navigateTo("main", 0);
            break;
        default: break;
        }
    }
    return true;
}

void CollectionScreen::render() {
    auto& R = m_renderer;
    R.fillRect(0, 0, SCREEN_W, SCREEN_H, Color::Background);
    R.drawHeader("Collections");

    if (m_loading) {
        R.drawLoadingOverlay("Loading collections...");
        R.drawStatusBar("");
        return;
    }

    if (!m_error.empty()) {
        R.drawErrorScreen(m_error);
        R.drawStatusBar("B Back");
        return;
    }

    if (m_collections.empty()) {
        R.drawTextCentered("No collections found.", 0, SCREEN_H / 2 - 20,
                           SCREEN_W, Color::TextDim, R.fontLarge());
        R.drawStatusBar("B Back");
        return;
    }

    int end = std::min(m_scrollOff + VISIBLE_ROWS,
                       static_cast<int>(m_collections.size()));
    for (int i = m_scrollOff; i < end; ++i) {
        const auto& c = m_collections[static_cast<size_t>(i)];
        int y         = CONTENT_Y + (i - m_scrollOff) * ITEM_H;
        bool selected = (i == m_selected);

        R.fillRect(0, y, SCREEN_W, ITEM_H,
                   selected ? Color::CardHover : Color::Background);

        R.drawText(c.name, 30, y + (ITEM_H - 26) / 2,
                   selected ? Color::TextWhite : Color::Text);

        std::string countStr = std::to_string(c.romCount) + " ROMs";
        int cw = R.textWidth(countStr, R.fontSmall());
        R.drawText(countStr, SCREEN_W - cw - 30, y + (ITEM_H - 20) / 2,
                   Color::TextDim, R.fontSmall());

        R.fillRect(0, y + ITEM_H - 1, SCREEN_W, 1, Color::Separator);
    }

    if (static_cast<int>(m_collections.size()) > VISIBLE_ROWS) {
        float ratio  = static_cast<float>(VISIBLE_ROWS) / m_collections.size();
        float offset = static_cast<float>(m_scrollOff) / m_collections.size();
        int   barH   = static_cast<int>(CONTENT_H * ratio);
        int   barY   = CONTENT_Y + static_cast<int>(CONTENT_H * offset);
        R.fillRect(SCREEN_W - 6, barY, 4, barH, Color::CardHover);
    }

    R.drawStatusBar("\xe2\x86\x91\xe2\x86\x93 Navigate  \xe2\x8f\xb5 Open Collection  B Back");
}
