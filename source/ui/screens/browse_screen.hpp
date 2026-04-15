#pragma once

#include "ui/renderer.hpp"
#include "ui/screens/screen.hpp"
#include "models/models.hpp"
#include "api/romm_client.hpp"

#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

// ---------------------------------------------------------------------------
// BrowseScreen — unified browsing with sidebar + content area
//
// Layout:
//   [=============== HEADER (60px) ================]
//   [  SIDEBAR  |       CONTENT AREA               ]
//   [  (300px)  |       (980px)                     ]
//   [           |  List view  OR  Grid view         ]
//   [=============== STATUS (44px) ================]
//
// Sidebar: Tabs (Platforms / Collections) + scrollable item list
// Content: List or Grid of ROMs with optional cover images
// ---------------------------------------------------------------------------

enum class BrowseTab  { Platforms, Collections };
enum class ViewMode   { List, Grid };
enum class BrowsePane { Sidebar, Content };

class BrowseScreen : public Screen {
public:
    BrowseScreen(Renderer& renderer, NavigateFn navigate,
                 romm::RommClient& client);
    ~BrowseScreen() override;

    void onEnter() override;
    bool update(const SDL_Event& event) override;
    void render() override;

private:
    romm::RommClient& m_client;

    // --- Sidebar data ---
    BrowseTab                       m_tab = BrowseTab::Platforms;
    std::vector<romm::Platform>     m_platforms;
    std::vector<romm::Collection>   m_collections;
    bool                            m_loadingSidebar = true;
    std::string                     m_sidebarError;

    // --- Content data ---
    std::vector<romm::Rom>          m_games;
    bool                            m_loadingGames = false;
    std::string                     m_gamesError;
    int                             m_loadedContextId = -1; // track which id is loaded

    // --- Focus & selection ---
    BrowsePane m_focusPane      = BrowsePane::Sidebar;
    int        m_sidebarSel     = 0;
    int        m_sidebarScroll  = 0;
    int        m_contentSel     = 0;
    int        m_contentScroll  = 0;

    // --- View mode ---
    ViewMode   m_viewMode = ViewMode::List;

    // --- Cover image cache ---
    std::unordered_map<int, SDL_Texture*> m_coverCache;
    std::unordered_set<int>               m_coverRequested; // IDs already queued

    // Queue entries: ROM ID + cover path (needed to build the download URL)
    struct CoverRequest { int romId; std::string coverPath; };
    std::deque<CoverRequest>              m_coverQueue;

    // Background thread for cover downloading
    struct CoverResult { int romId; std::vector<uint8_t> data; };
    std::thread            m_coverThread;
    std::atomic<bool>      m_coverStop{false};
    std::mutex             m_coverMutex;
    std::deque<CoverResult> m_coverResults;

    // --- Layout constants ---
    static constexpr int HEADER_H     = 60;
    static constexpr int STATUS_H     = 44;
    static constexpr int CONTENT_Y    = HEADER_H;
    static constexpr int CONTENT_H    = SCREEN_H - HEADER_H - STATUS_H;

    static constexpr int SIDEBAR_W    = 300;
    static constexpr int MAIN_X       = SIDEBAR_W;
    static constexpr int MAIN_W       = SCREEN_W - SIDEBAR_W;

    // Sidebar
    static constexpr int TAB_H        = 40;
    static constexpr int SIDEBAR_ITEM_H  = 48;
    static constexpr int SIDEBAR_LIST_Y  = CONTENT_Y + TAB_H;
    static constexpr int SIDEBAR_LIST_H  = CONTENT_H - TAB_H;
    static constexpr int SIDEBAR_VISIBLE = SIDEBAR_LIST_H / SIDEBAR_ITEM_H;

    // List view
    static constexpr int LIST_ITEM_H  = 70;
    static constexpr int LIST_VISIBLE = CONTENT_H / LIST_ITEM_H;

    // Grid view
    static constexpr int GRID_CELL_W  = 175;
    static constexpr int GRID_CELL_H  = 240;
    static constexpr int GRID_PAD     = 12;
    static constexpr int GRID_IMG_H   = 190;

    // --- Helper methods ---
    int  sidebarCount() const;
    int  gameCount() const;
    int  gridColumns() const;
    int  gridVisibleRows() const;
    int  gridTotalRows() const;
    void clampSidebar();
    void clampContentList();
    void clampContentGrid();

    void loadSidebar();
    void loadGamesForSelection();

    // Input
    void handleSidebarInput(SDL_Keycode key);
    void handleContentInput(SDL_Keycode key);
    void handleContentListInput(SDL_Keycode key);
    void handleContentGridInput(SDL_Keycode key);
    void switchTab();

    // Rendering
    void renderSidebar();
    void renderContent();
    void renderListView();
    void renderGridView();

    // Cover image management
    void requestVisibleCovers();
    void processCoverResults();
    void coverWorker();
    void clearCovers();
    void stopCoverThread();
};
