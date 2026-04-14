#include "ui/screens/game_screen.hpp"
#include "ui/renderer.hpp"

#include <algorithm>
#include <cctype>

static std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

GameScreen::GameScreen(Renderer& renderer, NavigateFn navigate,
                       romm::RommClient& client, int contextId,
                       GameListMode mode)
    : Screen(renderer, std::move(navigate))
    , m_client(client)
    , m_contextId(contextId)
    , m_mode(mode)
    , m_title(mode == GameListMode::Platform ? "Games" : "Collection")
{}

void GameScreen::onEnter() {
    m_loading    = true;
    m_error.clear();
    m_filter.clear();
    m_filterMode = false;
    m_selected   = 0;
    m_scrollOff  = 0;
    loadData();
}

void GameScreen::loadData() {
    if (m_mode == GameListMode::Platform)
        m_roms = m_client.getRoms(m_contextId, m_error);
    else
        m_roms = m_client.getRomsByCollection(m_contextId, m_error);

    // Sort alphabetically
    std::sort(m_roms.begin(), m_roms.end(),
              [](const romm::Rom& a, const romm::Rom& b) {
                  return a.name < b.name;
              });
    m_loading = false;
    applyFilter();
}

void GameScreen::applyFilter() {
    if (m_filter.empty()) {
        m_filtered = m_roms;
    } else {
        std::string low = toLower(m_filter);
        m_filtered.clear();
        for (const auto& r : m_roms) {
            if (toLower(r.name).find(low) != std::string::npos)
                m_filtered.push_back(r);
        }
    }
    m_selected  = 0;
    m_scrollOff = 0;
}

const std::vector<romm::Rom>& GameScreen::displayList() const {
    return m_filtered;
}

void GameScreen::clampScroll() {
    const auto& list = displayList();
    int maxSel = static_cast<int>(list.size()) - 1;
    if (m_selected < 0) m_selected = 0;
    if (m_selected > maxSel) m_selected = maxSel;

    if (m_selected < m_scrollOff)
        m_scrollOff = m_selected;
    if (m_selected >= m_scrollOff + VISIBLE_ROWS)
        m_scrollOff = m_selected - VISIBLE_ROWS + 1;
}

bool GameScreen::update(const SDL_Event& event) {
    if (m_loading) return true;

    if (m_filterMode) {
        if (event.type == SDL_TEXTINPUT) {
            m_filter += event.text.text;
            applyFilter();
            return true;
        }
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
            case SDLK_BACKSPACE:
                if (!m_filter.empty()) {
                    m_filter.pop_back();
                    applyFilter();
                }
                break;
            case SDLK_RETURN:
            case SDLK_ESCAPE:
                m_filterMode = false;
                SDL_StopTextInput();
                break;
            default: break;
            }
        }
        return true;
    }

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
        {
            const auto& list = displayList();
            if (!list.empty())
                navigateTo("detail", list[static_cast<size_t>(m_selected)].id);
            break;
        }
        case SDLK_y:   // Y button — search/filter
            m_filterMode = true;
            SDL_StartTextInput();
            break;
        case SDLK_b:
        case SDLK_B:
            navigateTo("main", 0);
            break;
        default: break;
        }
    }
    return true;
}

void GameScreen::render() {
    auto& R = m_renderer;
    R.fillRect(0, 0, SCREEN_W, SCREEN_H, Color::Background);

    std::string header = m_title;
    if (!m_filter.empty())
        header += "  [" + m_filter + "]";
    R.drawHeader(header);

    if (m_loading) {
        R.drawLoadingOverlay("Loading ROMs...");
        R.drawStatusBar("");
        return;
    }

    if (!m_error.empty()) {
        R.drawErrorScreen(m_error);
        R.drawStatusBar("B Back");
        return;
    }

    const auto& list = displayList();

    if (list.empty()) {
        std::string msg = m_filter.empty() ? "No ROMs found." : "No results for \"" + m_filter + "\"";
        R.drawTextCentered(msg, 0, SCREEN_H / 2 - 20,
                           SCREEN_W, Color::TextDim, R.fontLarge());
        R.drawStatusBar("Y Search  B Back");
        return;
    }

    // Draw list items
    int end = std::min(m_scrollOff + VISIBLE_ROWS, static_cast<int>(list.size()));
    for (int i = m_scrollOff; i < end; ++i) {
        const auto& rom = list[static_cast<size_t>(i)];
        int  y          = CONTENT_Y + (i - m_scrollOff) * ITEM_H;
        bool selected   = (i == m_selected);

        R.fillRect(0, y, SCREEN_W, ITEM_H,
                   selected ? Color::CardHover : Color::Background);

        // ROM name
        R.drawText(rom.name, 30, y + 8,
                   selected ? Color::TextWhite : Color::Text);

        // Platform name (dim)
        if (!rom.platformName.empty()) {
            R.drawText(rom.platformName, 30, y + 30,
                       Color::TextDim, R.fontSmall());
        }

        // File size (right-aligned)
        if (rom.fileSizeBytes > 0) {
            std::string sizeStr = rom.fileSizeStr();
            int sw = R.textWidth(sizeStr, R.fontSmall());
            R.drawText(sizeStr, SCREEN_W - sw - 30, y + (ITEM_H - 20) / 2,
                       Color::TextDim, R.fontSmall());
        }

        R.fillRect(0, y + ITEM_H - 1, SCREEN_W, 1, Color::Separator);
    }

    // Scroll indicator
    if (static_cast<int>(list.size()) > VISIBLE_ROWS) {
        float ratio  = static_cast<float>(VISIBLE_ROWS) / list.size();
        float offset = static_cast<float>(m_scrollOff) / list.size();
        int   barH   = static_cast<int>(CONTENT_H * ratio);
        int   barY   = CONTENT_Y + static_cast<int>(CONTENT_H * offset);
        R.fillRect(SCREEN_W - 6, barY, 4, barH, Color::CardHover);
    }

    std::string hint = "\xe2\x86\x91\xe2\x86\x93 Navigate  \xe2\x8f\xb5 Details  Y Search";
    if (m_filterMode) hint = "Type to search  Enter/Esc Exit search";
    hint += "  B Back";
    R.drawStatusBar(hint);
}
