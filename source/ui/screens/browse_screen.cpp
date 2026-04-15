#include "ui/screens/browse_screen.hpp"
#include "ui/renderer.hpp"

#include <algorithm>
#include <chrono>

// ---------------------------------------------------------------------------
// Local helper: truncate a string so it fits within maxWidth pixels
// ---------------------------------------------------------------------------
static std::string truncateText(const std::string& text, int maxWidth,
                                TTF_Font* font, Renderer& renderer) {
    std::string display = text;
    while (renderer.textWidth(display, font) > maxWidth && display.size() > 3) {
        display.pop_back();
        if (display.size() > 2) display.replace(display.size() - 2, 2, "..");
    }
    return display;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

BrowseScreen::BrowseScreen(Renderer& renderer, NavigateFn navigate,
                           romm::RommClient& client)
    : Screen(renderer, std::move(navigate))
    , m_client(client)
{}

BrowseScreen::~BrowseScreen() {
    stopCoverThread();

    // Destroy all cached textures (without restarting the worker thread)
    for (auto& [id, tex] : m_coverCache) {
        if (tex) SDL_DestroyTexture(tex);
    }
    m_coverCache.clear();
    m_coverRequested.clear();
    m_coverQueue.clear();
    m_coverResults.clear();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void BrowseScreen::onEnter() {
    m_loadingSidebar = true;
    m_sidebarError.clear();
    m_gamesError.clear();
    m_sidebarSel    = 0;
    m_sidebarScroll = 0;
    m_contentSel    = 0;
    m_contentScroll = 0;
    m_focusPane     = BrowsePane::Sidebar;
    m_loadedContextId = -1;
    m_games.clear();
    clearCovers();
    loadSidebar();

    // Start cover-download worker thread
    m_coverStop = false;
    if (!m_coverThread.joinable()) {
        m_coverThread = std::thread(&BrowseScreen::coverWorker, this);
    }
}

// ---------------------------------------------------------------------------
// Data loading
// ---------------------------------------------------------------------------

void BrowseScreen::loadSidebar() {
    m_loadingSidebar = true;
    m_sidebarError.clear();

    m_platforms   = m_client.getPlatforms(m_sidebarError);
    if (m_sidebarError.empty()) {
        std::string err2;
        m_collections = m_client.getCollections(err2);
        if (!err2.empty() && m_sidebarError.empty())
            m_sidebarError = err2;
    }

    // Sort platforms alphabetically
    std::sort(m_platforms.begin(), m_platforms.end(),
              [](const romm::Platform& a, const romm::Platform& b) {
                  return a.name < b.name;
              });
    std::sort(m_collections.begin(), m_collections.end(),
              [](const romm::Collection& a, const romm::Collection& b) {
                  return a.name < b.name;
              });

    m_loadingSidebar = false;
}

void BrowseScreen::loadGamesForSelection() {
    if (sidebarCount() == 0) return;

    int contextId = -1;
    if (m_tab == BrowseTab::Platforms && !m_platforms.empty())
        contextId = m_platforms[static_cast<size_t>(m_sidebarSel)].id;
    else if (m_tab == BrowseTab::Collections && !m_collections.empty())
        contextId = m_collections[static_cast<size_t>(m_sidebarSel)].id;

    if (contextId < 0) return;
    if (contextId == m_loadedContextId) return; // already loaded

    m_loadingGames = true;
    m_gamesError.clear();
    m_contentSel    = 0;
    m_contentScroll = 0;

    if (m_tab == BrowseTab::Platforms)
        m_games = m_client.getRoms(contextId, m_gamesError);
    else
        m_games = m_client.getRomsByCollection(contextId, m_gamesError);

    // Sort alphabetically
    std::sort(m_games.begin(), m_games.end(),
              [](const romm::Rom& a, const romm::Rom& b) {
                  return a.name < b.name;
              });

    m_loadedContextId = contextId;
    m_loadingGames    = false;

    // Clear old covers and queue new ones
    clearCovers();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int BrowseScreen::sidebarCount() const {
    return (m_tab == BrowseTab::Platforms)
        ? static_cast<int>(m_platforms.size())
        : static_cast<int>(m_collections.size());
}

int BrowseScreen::gameCount() const {
    return static_cast<int>(m_games.size());
}

int BrowseScreen::gridColumns() const {
    return std::max(1, (MAIN_W - GRID_PAD) / (GRID_CELL_W + GRID_PAD));
}

int BrowseScreen::gridVisibleRows() const {
    return std::max(1, CONTENT_H / (GRID_CELL_H + GRID_PAD));
}

int BrowseScreen::gridTotalRows() const {
    int cols = gridColumns();
    return (gameCount() + cols - 1) / cols;
}

void BrowseScreen::clampSidebar() {
    int count = sidebarCount();
    if (count == 0) { m_sidebarSel = 0; m_sidebarScroll = 0; return; }
    if (m_sidebarSel < 0) m_sidebarSel = 0;
    if (m_sidebarSel >= count) m_sidebarSel = count - 1;
    if (m_sidebarSel < m_sidebarScroll)
        m_sidebarScroll = m_sidebarSel;
    if (m_sidebarSel >= m_sidebarScroll + SIDEBAR_VISIBLE)
        m_sidebarScroll = m_sidebarSel - SIDEBAR_VISIBLE + 1;
}

void BrowseScreen::clampContentList() {
    int count = gameCount();
    if (count == 0) { m_contentSel = 0; m_contentScroll = 0; return; }
    if (m_contentSel < 0) m_contentSel = 0;
    if (m_contentSel >= count) m_contentSel = count - 1;
    if (m_contentSel < m_contentScroll)
        m_contentScroll = m_contentSel;
    if (m_contentSel >= m_contentScroll + LIST_VISIBLE)
        m_contentScroll = m_contentSel - LIST_VISIBLE + 1;
}

void BrowseScreen::clampContentGrid() {
    int count = gameCount();
    if (count == 0) { m_contentSel = 0; m_contentScroll = 0; return; }
    if (m_contentSel < 0) m_contentSel = 0;
    if (m_contentSel >= count) m_contentSel = count - 1;

    int cols = gridColumns();
    int row  = m_contentSel / cols;
    int visR = gridVisibleRows();
    if (row < m_contentScroll)
        m_contentScroll = row;
    if (row >= m_contentScroll + visR)
        m_contentScroll = row - visR + 1;
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

bool BrowseScreen::update(const SDL_Event& event) {
    if (event.type != SDL_KEYDOWN) return true;
    SDL_Keycode key = event.key.keysym.sym;

    // Global: L/R to switch tabs
    if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
        if (m_focusPane == BrowsePane::Sidebar) {
            switchTab();
        } else {
            // Page up/down in content
            if (m_viewMode == ViewMode::List) {
                if (key == SDLK_PAGEUP)  m_contentSel -= LIST_VISIBLE;
                else                     m_contentSel += LIST_VISIBLE;
                clampContentList();
            } else {
                int cols = gridColumns();
                int visR = gridVisibleRows();
                if (key == SDLK_PAGEUP)  m_contentSel -= cols * visR;
                else                     m_contentSel += cols * visR;
                clampContentGrid();
            }
        }
        return true;
    }

    if (m_focusPane == BrowsePane::Sidebar)
        handleSidebarInput(key);
    else
        handleContentInput(key);

    return true;
}

void BrowseScreen::switchTab() {
    m_tab = (m_tab == BrowseTab::Platforms)
        ? BrowseTab::Collections : BrowseTab::Platforms;
    m_sidebarSel    = 0;
    m_sidebarScroll = 0;
    // Don't clear games — they stay until user selects something new
}

void BrowseScreen::handleSidebarInput(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP:
        --m_sidebarSel;
        clampSidebar();
        break;
    case SDLK_DOWN:
        ++m_sidebarSel;
        clampSidebar();
        break;
    case SDLK_RIGHT:
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        // Load games for the selected item, then switch focus to content
        loadGamesForSelection();
        m_focusPane = BrowsePane::Content;
        break;
    case SDLK_b:
        navigateTo("main", 0);
        break;
    case SDLK_y:
        m_viewMode = (m_viewMode == ViewMode::List) ? ViewMode::Grid : ViewMode::List;
        m_contentSel    = 0;
        m_contentScroll = 0;
        break;
    default:
        break;
    }
}

void BrowseScreen::handleContentInput(SDL_Keycode key) {
    if (m_viewMode == ViewMode::List)
        handleContentListInput(key);
    else
        handleContentGridInput(key);
}

void BrowseScreen::handleContentListInput(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP:
        --m_contentSel;
        clampContentList();
        break;
    case SDLK_DOWN:
        ++m_contentSel;
        clampContentList();
        break;
    case SDLK_LEFT:
    case SDLK_b:
        m_focusPane = BrowsePane::Sidebar;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (!m_games.empty())
            navigateTo("detail", m_games[static_cast<size_t>(m_contentSel)].id);
        break;
    case SDLK_y:
        m_viewMode = ViewMode::Grid;
        m_contentScroll = 0;
        clampContentGrid();
        break;
    default:
        break;
    }
}

void BrowseScreen::handleContentGridInput(SDL_Keycode key) {
    int cols = gridColumns();

    switch (key) {
    case SDLK_UP:
        m_contentSel -= cols;
        clampContentGrid();
        break;
    case SDLK_DOWN:
        m_contentSel += cols;
        clampContentGrid();
        break;
    case SDLK_LEFT:
        if (m_contentSel % cols == 0) {
            // At first column — switch to sidebar
            m_focusPane = BrowsePane::Sidebar;
        } else {
            --m_contentSel;
            clampContentGrid();
        }
        break;
    case SDLK_RIGHT:
        if ((m_contentSel % cols) < cols - 1 && m_contentSel + 1 < gameCount()) {
            ++m_contentSel;
            clampContentGrid();
        }
        break;
    case SDLK_b:
        m_focusPane = BrowsePane::Sidebar;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (!m_games.empty())
            navigateTo("detail", m_games[static_cast<size_t>(m_contentSel)].id);
        break;
    case SDLK_y:
        m_viewMode = ViewMode::List;
        m_contentScroll = 0;
        clampContentList();
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void BrowseScreen::render() {
    auto& R = m_renderer;

    // Process any completed cover downloads
    processCoverResults();

    // Queue covers for visible items
    requestVisibleCovers();

    R.fillRect(0, 0, SCREEN_W, SCREEN_H, Color::Background);
    R.drawHeader("RomM2Switch");

    if (m_loadingSidebar) {
        R.drawLoadingOverlay("Loading platforms & collections...");
        R.drawStatusBar("");
        return;
    }

    renderSidebar();
    renderContent();

    // Status bar
    std::string hint;
    if (m_focusPane == BrowsePane::Sidebar) {
        hint = "Up/Down Navigate  L/R Tab  A/Right Open  Y View  B Back";
    } else {
        if (m_viewMode == ViewMode::List)
            hint = "Up/Down Navigate  A Details  Left/B Sidebar  Y Grid";
        else
            hint = "DPad Navigate  A Details  B Sidebar  Y List";
    }
    R.drawStatusBar(hint);
}

void BrowseScreen::renderSidebar() {
    auto& R = m_renderer;
    bool focused = (m_focusPane == BrowsePane::Sidebar);

    // Sidebar background
    R.fillRect(0, CONTENT_Y, SIDEBAR_W, CONTENT_H, Color::SidebarBg);

    // Separator line between sidebar and content
    R.fillRect(SIDEBAR_W - 1, CONTENT_Y, 1, CONTENT_H, Color::Separator);

    // --- Tab bar ---
    int tabW = SIDEBAR_W / 2;
    bool isPlatforms = (m_tab == BrowseTab::Platforms);

    R.fillRect(0, CONTENT_Y, tabW, TAB_H,
               isPlatforms ? Color::TabActive : Color::TabInactive);
    R.fillRect(tabW, CONTENT_Y, tabW, TAB_H,
               isPlatforms ? Color::TabInactive : Color::TabActive);

    R.drawTextCentered("Platforms", 0, CONTENT_Y + (TAB_H - 20) / 2, tabW,
                       isPlatforms ? Color::TextWhite : Color::TextDim, R.fontSmall());
    R.drawTextCentered("Collections", tabW, CONTENT_Y + (TAB_H - 20) / 2, tabW,
                       isPlatforms ? Color::TextDim : Color::TextWhite, R.fontSmall());

    // Separator below tabs
    R.fillRect(0, SIDEBAR_LIST_Y, SIDEBAR_W, 1, Color::Separator);

    // --- Item list ---
    if (!m_sidebarError.empty()) {
        R.drawTextCentered("Error", 0, SIDEBAR_LIST_Y + 40, SIDEBAR_W,
                           Color::Error, R.fontSmall());
        return;
    }

    int count = sidebarCount();
    if (count == 0) {
        R.drawTextCentered("(empty)", 0, SIDEBAR_LIST_Y + 40, SIDEBAR_W,
                           Color::TextDim, R.fontSmall());
        return;
    }

    int end = std::min(m_sidebarScroll + SIDEBAR_VISIBLE, count);
    for (int i = m_sidebarScroll; i < end; ++i) {
        int y = SIDEBAR_LIST_Y + (i - m_sidebarScroll) * SIDEBAR_ITEM_H;
        bool selected = (i == m_sidebarSel) && focused;

        R.fillRect(0, y, SIDEBAR_W - 1, SIDEBAR_ITEM_H,
                   selected ? Color::CardHover : Color::SidebarBg);

        // Item name
        std::string name;
        std::string countStr;
        if (m_tab == BrowseTab::Platforms) {
            const auto& p = m_platforms[static_cast<size_t>(i)];
            name     = p.name;
            countStr = std::to_string(p.romCount);
        } else {
            const auto& c = m_collections[static_cast<size_t>(i)];
            name     = c.name;
            countStr = std::to_string(c.romCount);
        }

        // Truncate long names
        int maxNameW = SIDEBAR_W - 80;
        std::string display = truncateText(name, maxNameW, R.fontSmall(), R);

        R.drawText(display, 12, y + (SIDEBAR_ITEM_H - 20) / 2,
                   selected ? Color::TextWhite : Color::Text, R.fontSmall());

        // ROM count (right-aligned)
        int cw = R.textWidth(countStr, R.fontSmall());
        R.drawText(countStr, SIDEBAR_W - cw - 16, y + (SIDEBAR_ITEM_H - 20) / 2,
                   Color::TextDim, R.fontSmall());

        // Separator
        R.fillRect(12, y + SIDEBAR_ITEM_H - 1, SIDEBAR_W - 24, 1, Color::Separator);
    }

    // Scroll indicator for sidebar
    if (count > SIDEBAR_VISIBLE) {
        float ratio  = static_cast<float>(SIDEBAR_VISIBLE) / count;
        float offset = static_cast<float>(m_sidebarScroll) / count;
        int   barH   = std::max(10, static_cast<int>(SIDEBAR_LIST_H * ratio));
        int   barY   = SIDEBAR_LIST_Y + static_cast<int>(SIDEBAR_LIST_H * offset);
        R.fillRect(SIDEBAR_W - 5, barY, 3, barH, Color::CardHover);
    }

    // Highlight indicator when sidebar is not focused but has selection
    if (!focused && m_sidebarSel >= m_sidebarScroll && m_sidebarSel < end) {
        int y = SIDEBAR_LIST_Y + (m_sidebarSel - m_sidebarScroll) * SIDEBAR_ITEM_H;
        R.fillRect(0, y, 3, SIDEBAR_ITEM_H, Color::CardHover);
    }
}

void BrowseScreen::renderContent() {
    auto& R = m_renderer;

    if (m_loadingGames) {
        R.drawTextCentered("Loading...", MAIN_X, SCREEN_H / 2 - 20, MAIN_W,
                           Color::TextDim, R.fontLarge());
        return;
    }

    if (!m_gamesError.empty()) {
        R.drawTextCentered("Error: " + m_gamesError, MAIN_X, SCREEN_H / 2 - 20, MAIN_W,
                           Color::Error, R.fontSmall());
        return;
    }

    if (m_games.empty()) {
        std::string msg = (m_loadedContextId < 0)
            ? "Select a platform or collection"
            : "No ROMs found";
        R.drawTextCentered(msg, MAIN_X, SCREEN_H / 2 - 20, MAIN_W,
                           Color::TextDim, R.fontLarge());
        return;
    }

    if (m_viewMode == ViewMode::List)
        renderListView();
    else
        renderGridView();
}

void BrowseScreen::renderListView() {
    auto& R = m_renderer;
    bool focused = (m_focusPane == BrowsePane::Content);

    int end = std::min(m_contentScroll + LIST_VISIBLE, gameCount());
    for (int i = m_contentScroll; i < end; ++i) {
        const auto& rom = m_games[static_cast<size_t>(i)];
        int y       = CONTENT_Y + (i - m_contentScroll) * LIST_ITEM_H;
        bool selected = (i == m_contentSel) && focused;

        R.fillRect(MAIN_X, y, MAIN_W, LIST_ITEM_H,
                   selected ? Color::CardHover : Color::Background);

        int textX = MAIN_X + 16;

        // Show small cover thumbnail in list view
        auto it = m_coverCache.find(rom.id);
        if (it != m_coverCache.end() && it->second) {
            int thumbH = LIST_ITEM_H - 10;
            int thumbW = thumbH * 3 / 4; // approximate aspect ratio
            R.drawTextureFit(it->second, MAIN_X + 10, y + 5, thumbW, thumbH);
            textX = MAIN_X + 10 + thumbW + 10;
        }

        // ROM name
        R.drawText(rom.name, textX, y + 14,
                   selected ? Color::TextWhite : Color::Text);

        // Sub-info
        std::string sub = rom.platformName;
        if (rom.fileSizeBytes > 0) {
            if (!sub.empty()) sub += "  |  ";
            sub += rom.fileSizeStr();
        }
        if (!sub.empty()) {
            R.drawText(sub, textX, y + 40,
                       Color::TextDim, R.fontSmall());
        }

        // Separator
        R.fillRect(MAIN_X, y + LIST_ITEM_H - 1, MAIN_W, 1, Color::Separator);
    }

    // Scroll indicator
    if (gameCount() > LIST_VISIBLE) {
        float ratio  = static_cast<float>(LIST_VISIBLE) / gameCount();
        float offset = static_cast<float>(m_contentScroll) / gameCount();
        int   barH   = std::max(10, static_cast<int>(CONTENT_H * ratio));
        int   barY   = CONTENT_Y + static_cast<int>(CONTENT_H * offset);
        R.fillRect(SCREEN_W - 6, barY, 4, barH, Color::CardHover);
    }
}

void BrowseScreen::renderGridView() {
    auto& R = m_renderer;
    bool focused = (m_focusPane == BrowsePane::Content);

    int cols = gridColumns();
    int visR = gridVisibleRows();

    // Center the grid horizontally within the content area
    int totalGridW = cols * GRID_CELL_W + (cols - 1) * GRID_PAD;
    int gridOffsetX = MAIN_X + (MAIN_W - totalGridW) / 2;

    int startRow = m_contentScroll;
    int endRow   = std::min(startRow + visR + 1, gridTotalRows());

    for (int row = startRow; row < endRow; ++row) {
        for (int col = 0; col < cols; ++col) {
            int idx = row * cols + col;
            if (idx >= gameCount()) break;

            const auto& rom = m_games[static_cast<size_t>(idx)];
            int cellX = gridOffsetX + col * (GRID_CELL_W + GRID_PAD);
            int cellY = CONTENT_Y + (row - startRow) * (GRID_CELL_H + GRID_PAD) + GRID_PAD;

            bool selected = (idx == m_contentSel) && focused;

            // Cell background
            R.fillRect(cellX, cellY, GRID_CELL_W, GRID_CELL_H,
                       selected ? Color::CardHover : Color::Card);

            // Cover image area
            auto it = m_coverCache.find(rom.id);
            if (it != m_coverCache.end() && it->second) {
                R.drawTextureFit(it->second, cellX + 2, cellY + 2,
                                 GRID_CELL_W - 4, GRID_IMG_H - 4);
            } else {
                // Placeholder
                R.fillRect(cellX + 2, cellY + 2, GRID_CELL_W - 4, GRID_IMG_H - 4,
                           Color::Background);
                // Show a small icon/text placeholder
                R.drawTextCentered("[No Cover]", cellX, cellY + GRID_IMG_H / 2 - 16,
                                   GRID_CELL_W, Color::TextDim, R.fontLarge());
            }

            // Game name below the image
            int nameY = cellY + GRID_IMG_H + 4;
            int nameH = GRID_CELL_H - GRID_IMG_H - 4;
            int maxW = GRID_CELL_W - 8;

            // Truncate name to fit
            std::string display = truncateText(rom.name, maxW, R.fontSmall(), R);
            R.drawTextCentered(display, cellX, nameY, GRID_CELL_W,
                               selected ? Color::TextWhite : Color::Text, R.fontSmall());

            // Second line: platform name (dim)
            if (!rom.platformName.empty() && nameH > 36) {
                std::string plat = truncateText(rom.platformName, maxW, R.fontSmall(), R);
                R.drawTextCentered(plat, cellX, nameY + 20, GRID_CELL_W,
                                   Color::TextDim, R.fontSmall());
            }

            // Selection border
            if (selected) {
                R.drawRect(cellX, cellY, GRID_CELL_W, GRID_CELL_H, Color::TextWhite);
            }
        }
    }

    // Scroll indicator
    int totalR = gridTotalRows();
    if (totalR > visR) {
        float ratio  = static_cast<float>(visR) / totalR;
        float offset = static_cast<float>(m_contentScroll) / totalR;
        int   barH   = std::max(10, static_cast<int>(CONTENT_H * ratio));
        int   barY   = CONTENT_Y + static_cast<int>(CONTENT_H * offset);
        R.fillRect(SCREEN_W - 6, barY, 4, barH, Color::CardHover);
    }
}

// ---------------------------------------------------------------------------
// Cover image management
// ---------------------------------------------------------------------------

void BrowseScreen::requestVisibleCovers() {
    if (m_games.empty()) return;

    std::lock_guard<std::mutex> lock(m_coverMutex);

    auto queueCover = [&](int idx) {
        if (idx < 0 || idx >= gameCount()) return;
        const auto& rom = m_games[static_cast<size_t>(idx)];
        if (!rom.hasCover()) return;
        if (m_coverCache.count(rom.id)) return;
        if (m_coverRequested.count(rom.id)) return;
        m_coverRequested.insert(rom.id);
        m_coverQueue.push_back({rom.id, rom.coverPathSmall});
    };

    if (m_viewMode == ViewMode::List) {
        int end = std::min(m_contentScroll + LIST_VISIBLE, gameCount());
        for (int i = m_contentScroll; i < end; ++i)
            queueCover(i);
    } else {
        int cols = gridColumns();
        int visR = gridVisibleRows();
        int startRow = m_contentScroll;
        int endRow   = std::min(startRow + visR + 1, gridTotalRows());
        for (int row = startRow; row < endRow; ++row) {
            for (int col = 0; col < cols; ++col) {
                queueCover(row * cols + col);
            }
        }
    }
}

void BrowseScreen::processCoverResults() {
    std::lock_guard<std::mutex> lock(m_coverMutex);
    while (!m_coverResults.empty()) {
        auto result = std::move(m_coverResults.front());
        m_coverResults.pop_front();

        if (m_coverCache.find(result.romId) == m_coverCache.end()) {
            SDL_Texture* tex = m_renderer.loadTextureFromMemory(result.data);
            if (tex) {
                m_coverCache[result.romId] = tex;
            }
        }
    }
}

void BrowseScreen::coverWorker() {
    while (!m_coverStop.load()) {
        CoverRequest req;
        req.romId = -1;
        {
            std::lock_guard<std::mutex> lock(m_coverMutex);
            if (!m_coverQueue.empty()) {
                req = std::move(m_coverQueue.front());
                m_coverQueue.pop_front();
            }
        }

        if (req.romId < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // Skip if already cached
        {
            std::lock_guard<std::mutex> lock(m_coverMutex);
            if (m_coverCache.count(req.romId)) continue;
        }

        std::string error;
        auto data = m_client.fetchCoverData(req.coverPath, error);

        if (!data.empty()) {
            std::lock_guard<std::mutex> lock(m_coverMutex);
            m_coverResults.push_back({req.romId, std::move(data)});
        }
    }
}

void BrowseScreen::clearCovers() {
    // Stop the worker thread before touching shared state
    stopCoverThread();

    // Destroy all cached textures (main thread only, safe without lock)
    for (auto& [id, tex] : m_coverCache) {
        if (tex) SDL_DestroyTexture(tex);
    }
    m_coverCache.clear();
    m_coverRequested.clear();

    // Clear queues (thread is stopped, but lock for consistency)
    {
        std::lock_guard<std::mutex> lock(m_coverMutex);
        m_coverQueue.clear();
        m_coverResults.clear();
    }

    // Restart the worker thread
    m_coverStop = false;
    m_coverThread = std::thread(&BrowseScreen::coverWorker, this);
}

void BrowseScreen::stopCoverThread() {
    m_coverStop = true;
    if (m_coverThread.joinable()) {
        m_coverThread.join();
    }
}
