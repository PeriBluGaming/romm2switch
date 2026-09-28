#pragma once

#include "api/romm_client.hpp"
#include "models/models.hpp"
#include "ui/download_queue.hpp"
#include "ui/renderer.hpp"
#include "ui/screens/screen.hpp"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class MainTab { Start = 0, Platforms, Collections, Search, Queues };
enum class ViewMode { List, Grid };
enum class FocusArea {
    HeaderTabs,
    HeaderSettings,
    Start,
    Platforms,
    Collections,
    SearchBox,
    SearchResults,
    Queues
};

class BrowseScreen : public Screen {
public:
    BrowseScreen(Renderer& renderer, NavigateFn navigate,
                 romm::RommClient* client,
                 bool hasConfig,
                 bool loggedIn,
                 std::string loginError,
                 DownloadQueue& downloads);
    ~BrowseScreen() override;

    void onEnter() override;
    bool update(const SDL_Event& event) override;
    void render() override;
    void setSessionState(romm::RommClient* client,
                         bool hasConfig,
                         bool loggedIn,
                         const std::string& loginError);

private:
    romm::RommClient* m_client;
    bool              m_hasConfig;
    bool              m_loggedIn;
    std::string       m_loginError;
    DownloadQueue&    m_downloads;

    MainTab   m_tab = MainTab::Start;
    FocusArea m_focus = FocusArea::Start;
    ViewMode  m_viewMode = ViewMode::List;

    std::vector<romm::Platform>   m_platforms;
    std::vector<romm::Collection> m_collections;
    bool                          m_loadingLibrary = true;
    std::string                   m_libraryError;

    int         m_platformSel = 0;
    int         m_platformScroll = 0;
    int         m_collectionSel = 0;
    int         m_collectionScroll = 0;
    int         m_startSel = 0;
    int         m_queueSel = 0;
    int         m_queueScroll = 0;

    int                    m_platformContextId = -1;
    std::string            m_platformContextName;
    std::vector<romm::Rom> m_platformGames;
    int                    m_platformGameSel = 0;
    int                    m_platformGameScroll = 0;
    bool                   m_loadingPlatformGames = false;
    std::string            m_platformGamesError;

    int                    m_collectionContextId = -1;
    std::string            m_collectionContextName;
    std::vector<romm::Rom> m_collectionGames;
    int                    m_collectionGameSel = 0;
    int                    m_collectionGameScroll = 0;
    bool                   m_loadingCollectionGames = false;
    std::string            m_collectionGamesError;

    std::vector<romm::Rom> m_searchLibrary;
    std::vector<romm::Rom> m_searchResults;
    bool                   m_searchIndexLoaded = false;
    bool                   m_loadingSearch = false;
    bool                   m_searchEditing = false;
    std::string            m_searchQuery;
    std::string            m_searchError;
    int                    m_searchSel = 0;
    int                    m_searchScroll = 0;

    std::unordered_map<int, SDL_Texture*> m_coverCache;
    std::unordered_set<int>               m_coverRequested;

    struct CoverRequest { int romId; std::string coverPath; };
    struct CoverResult { int romId; std::vector<uint8_t> data; };
    std::deque<CoverRequest> m_coverQueue;
    std::deque<CoverResult>  m_coverResults;
    std::thread              m_coverThread;
    std::atomic<bool>        m_coverStop{false};
    std::mutex               m_coverMutex;

    static constexpr int HEADER_H        = 104;
    static constexpr int STATUS_H        = 44;
    static constexpr int CONTENT_Y       = HEADER_H;
    static constexpr int CONTENT_H       = SCREEN_H - HEADER_H - STATUS_H;
    static constexpr int CONTENT_X       = 28;
    static constexpr int CONTENT_W       = SCREEN_W - CONTENT_X * 2;
    static constexpr int SECTION_TOP_PAD = 28;
    static constexpr int LIST_ITEM_H     = 76;
    static constexpr int GRID_CELL_W     = 210;
    static constexpr int GRID_CELL_H     = 248;
    static constexpr int GRID_PAD        = 18;
    static constexpr int GRID_IMG_H      = 184;
    static constexpr int SEARCH_BOX_H    = 56;
    static constexpr int QUEUE_VISIBLE   = 6;

    void loadLibrary();
    void loadPlatformGames();
    void loadCollectionGames();
    void ensureSearchIndex();
    void applySearch();

    int  listVisibleRows(int topOffset = 0) const;
    int  gridColumns() const;
    int  gridVisibleRows(int topOffset = 0) const;
    void clampSelection(int& selected, int& scroll, int count, int visible) const;
    void clampGridSelection(int& selected, int& scroll, int count, int visibleRows) const;
    void moveHeaderToBody();
    void switchTab(int delta);
    void toggleViewMode();
    void leaveSearchEditing();
    bool clientReady() const;

    void handleHeaderTabsInput(SDL_Keycode key);
    void handleHeaderSettingsInput(SDL_Keycode key);
    void handleStartInput(SDL_Keycode key);
    void handlePlatformInput(SDL_Keycode key);
    void handleCollectionInput(SDL_Keycode key);
    void handleSearchBoxInput(SDL_Keycode key);
    void handleSearchResultsInput(SDL_Keycode key);
    void handleQueueInput(SDL_Keycode key);

    void renderHeader();
    void renderStartTab();
    void renderPlatformsTab();
    void renderCollectionsTab();
    void renderSearchTab();
    void renderQueuesTab();
    void renderDisconnectedState(const std::string& title, const std::string& body);
    void renderCollectionLikeGrid(const std::vector<romm::Platform>& items, int selected, int scroll, bool focused);
    void renderCollectionLikeList(const std::vector<romm::Platform>& items, int selected, int scroll, bool focused);
    void renderCollectionLikeGrid(const std::vector<romm::Collection>& items, int selected, int scroll, bool focused);
    void renderCollectionLikeList(const std::vector<romm::Collection>& items, int selected, int scroll, bool focused);
    void renderGameGrid(const std::vector<romm::Rom>& games, int selected, int scroll, bool focused, int topOffset = 0);
    void renderGameList(const std::vector<romm::Rom>& games, int selected, int scroll, bool focused, int topOffset = 0);
    void renderQueueList(const std::vector<QueueItemSnapshot>& items);

    void requestVisibleCovers();
    void requestCover(int romId, const std::string& path);
    void processCoverResults();
    void coverWorker();
    void clearCovers();
    void stopCoverThread();
};
