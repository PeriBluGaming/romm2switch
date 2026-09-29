#include "ui/screens/browse_screen.hpp"

#include "ui/renderer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>

namespace {

static std::string truncateText(const std::string& text, int maxWidth,
                                TTF_Font* font, Renderer& renderer) {
    std::string display = text;
    while (renderer.textWidth(display, font) > maxWidth && display.size() > 3) {
        display.pop_back();
        if (display.size() > 2) display.replace(display.size() - 2, 2, "..");
    }
    return display;
}

static std::string toLower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string initialsFor(const std::string& text) {
    std::string out;
    bool take = true;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) && take) {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            take = false;
            if (out.size() == 2) break;
        } else if (c == ' ' || c == '-' || c == '_') {
            take = true;
        }
    }
    if (out.empty() && !text.empty())
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(text.front())));
    return out;
}

static std::string formatBytes(long long bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024LL * 1024) return std::to_string(bytes / 1024) + " KB";
    if (bytes < 1024LL * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
    return std::to_string(bytes / (1024LL * 1024 * 1024)) + " GB";
}

static std::string formatEta(int seconds) {
    if (seconds < 0) return "--";
    int mins = seconds / 60;
    int secs = seconds % 60;
    if (mins <= 0) return std::to_string(secs) + "s";
    return std::to_string(mins) + "m " + std::to_string(secs) + "s";
}

static const char* queueStateLabel(QueueItemState state) {
    switch (state) {
    case QueueItemState::Queued:      return "Queued";
    case QueueItemState::Downloading: return "Downloading";
    case QueueItemState::Completed:   return "Completed";
    case QueueItemState::Failed:      return "Failed";
    case QueueItemState::Cancelled:   return "Cancelled";
    }
    return "";
}

} // namespace

BrowseScreen::BrowseScreen(Renderer& renderer, NavigateFn navigate,
                           romm::RommClient* client,
                           bool hasConfig,
                           bool loggedIn,
                           std::string loginError,
                           DownloadQueue& downloads)
    : Screen(renderer, std::move(navigate))
    , m_client(client)
    , m_hasConfig(hasConfig)
    , m_loggedIn(loggedIn)
    , m_loginError(std::move(loginError))
    , m_downloads(downloads)
{}

BrowseScreen::~BrowseScreen() {
    stopCoverThread();
    for (auto& [id, tex] : m_coverCache) {
        if (tex) SDL_DestroyTexture(tex);
    }
}

void BrowseScreen::pauseForClientSwap() {
    stopCoverThread();
}

void BrowseScreen::setSessionState(romm::RommClient* client,
                                   bool hasConfig,
                                   bool loggedIn,
                                   const std::string& loginError) {
    bool wasReady = clientReady();
    m_client = client;
    m_hasConfig = hasConfig;
    m_loggedIn = loggedIn;
    m_loginError = loginError;
    if (!clientReady()) {
        m_searchIndexLoaded = false;
        m_searchLibrary.clear();
        m_searchResults.clear();
        m_platforms.clear();
        m_collections.clear();
        m_platformGames.clear();
        m_collectionGames.clear();
        m_platformContextId = -1;
        m_collectionContextId = -1;
        m_libraryError.clear();
    } else if (!wasReady || m_platforms.empty()) {
        loadLibrary();
    }
    if (!m_coverThread.joinable()) {
        m_coverStop = false;
        m_coverThread = std::thread(&BrowseScreen::coverWorker, this);
    }
}

void BrowseScreen::onEnter() {
    m_hasConfig = m_hasConfig || clientReady();
    loadLibrary();
    clearCovers();
}

bool BrowseScreen::update(const SDL_Event& event) {
    if (m_searchEditing) {
        if (event.type == SDL_TEXTINPUT) {
            m_searchQuery += event.text.text;
            applySearch();
            return true;
        }
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
            case SDLK_BACKSPACE:
                if (!m_searchQuery.empty()) {
                    m_searchQuery.pop_back();
                    applySearch();
                }
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_ESCAPE:
            case SDLK_b:
                leaveSearchEditing();
                break;
            default:
                break;
            }
        }
        return true;
    }

    if (event.type != SDL_KEYDOWN) return true;
    SDL_Keycode key = event.key.keysym.sym;

    if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
        switchTab(key == SDLK_PAGEUP ? -1 : 1);
        return true;
    }

    switch (m_focus) {
    case FocusArea::HeaderTabs:     handleHeaderTabsInput(key); break;
    case FocusArea::HeaderSettings: handleHeaderSettingsInput(key); break;
    case FocusArea::Start:          handleStartInput(key); break;
    case FocusArea::Platforms:      handlePlatformInput(key); break;
    case FocusArea::Collections:    handleCollectionInput(key); break;
    case FocusArea::SearchBox:      handleSearchBoxInput(key); break;
    case FocusArea::SearchResults:  handleSearchResultsInput(key); break;
    case FocusArea::Queues:         handleQueueInput(key); break;
    }
    return true;
}

void BrowseScreen::render() {
    auto& R = m_renderer;
    m_queueSnapshot = m_downloads.items();

    bool activeTabShowsCovers =
        (m_tab == MainTab::Platforms && m_platformContextId >= 0) ||
        (m_tab == MainTab::Collections && m_collectionContextId >= 0) ||
        m_tab == MainTab::Search ||
        m_tab == MainTab::Queues;
    if (activeTabShowsCovers) {
        processCoverResults();
        requestVisibleCovers();
    }

    R.fillRect(0, 0, SCREEN_W, SCREEN_H, Color::Background);
    renderHeader();

    switch (m_tab) {
    case MainTab::Start:       renderStartTab(); break;
    case MainTab::Platforms:   renderPlatformsTab(); break;
    case MainTab::Collections: renderCollectionsTab(); break;
    case MainTab::Search:      renderSearchTab(); break;
    case MainTab::Queues:      renderQueuesTab(); break;
    }

    std::string hint = "L/R Tabs  Up/Down Navigate  A Select";
    if (m_focus == FocusArea::HeaderTabs)
        hint = "Left/Right Select Tab  Down Enter  A Keep Current";
    else if (m_focus == FocusArea::HeaderSettings)
        hint = "A Open Settings  Left Return  Down Close Header";
    else if (m_tab == MainTab::Start)
        hint = "Up/Down Navigate  A Open  L/R Tabs";
    else if (m_tab == MainTab::Platforms || m_tab == MainTab::Collections)
        hint = "A Open  Y View  B Back  L/R Tabs";
    else if (m_tab == MainTab::Search)
        hint = m_searchEditing
            ? "Type to Search  Enter/B Finish"
            : "A Search  Y View  B Back  L/R Tabs";
    else if (m_tab == MainTab::Queues)
        hint = "Up/Down Navigate  A Details  B Home  L/R Tabs";

    R.drawStatusBar(hint);
}

void BrowseScreen::loadLibrary() {
    m_loadingLibrary = true;
    m_libraryError.clear();
    m_platforms.clear();
    m_collections.clear();

    if (!clientReady()) {
        m_loadingLibrary = false;
        return;
    }

    m_platforms = m_client->getPlatforms(m_libraryError);
    if (m_libraryError.empty()) {
        std::string collectionsError;
        m_collections = m_client->getCollections(collectionsError);
        if (!collectionsError.empty()) m_libraryError = collectionsError;
    }

    std::sort(m_platforms.begin(), m_platforms.end(),
              [](const romm::Platform& a, const romm::Platform& b) {
                  return a.name < b.name;
              });
    std::sort(m_collections.begin(), m_collections.end(),
              [](const romm::Collection& a, const romm::Collection& b) {
                  return a.name < b.name;
              });
    m_loadingLibrary = false;
}

void BrowseScreen::loadPlatformGames() {
    m_platformGames.clear();
    m_platformGamesError.clear();
    m_platformGameSel = 0;
    m_platformGameScroll = 0;

    if (!clientReady() || m_platforms.empty()) return;
    m_loadingPlatformGames = true;
    const auto& platform = m_platforms[static_cast<size_t>(m_platformSel)];
    m_platformContextId = platform.id;
    m_platformContextName = platform.name;
    m_platformGames = m_client->getRoms(platform.id, m_platformGamesError);
    std::sort(m_platformGames.begin(), m_platformGames.end(),
              [](const romm::Rom& a, const romm::Rom& b) {
                  return a.name < b.name;
              });
    m_loadingPlatformGames = false;
    clearCovers();
}

void BrowseScreen::loadCollectionGames() {
    m_collectionGames.clear();
    m_collectionGamesError.clear();
    m_collectionGameSel = 0;
    m_collectionGameScroll = 0;

    if (!clientReady() || m_collections.empty()) return;
    m_loadingCollectionGames = true;
    const auto& collection = m_collections[static_cast<size_t>(m_collectionSel)];
    m_collectionContextId = collection.id;
    m_collectionContextName = collection.name;
    m_collectionGames = m_client->getRomsByCollection(collection.id, m_collectionGamesError);
    std::sort(m_collectionGames.begin(), m_collectionGames.end(),
              [](const romm::Rom& a, const romm::Rom& b) {
                  return a.name < b.name;
              });
    m_loadingCollectionGames = false;
    clearCovers();
}

void BrowseScreen::ensureSearchIndex() {
    if (m_searchIndexLoaded || !clientReady()) return;

    m_loadingSearch = true;
    m_searchError.clear();
    m_searchLibrary.clear();
    std::unordered_set<int> seen;

    std::vector<romm::Platform> sourcePlatforms = m_platforms;
    if (sourcePlatforms.empty()) {
        std::string error;
        sourcePlatforms = m_client->getPlatforms(error);
        if (!error.empty()) {
            m_searchError = error;
            m_loadingSearch = false;
            return;
        }
    }

    for (const auto& platform : sourcePlatforms) {
        std::string error;
        auto roms = m_client->getRoms(platform.id, error);
        if (!error.empty()) {
            m_searchError = error;
            break;
        }
        for (auto& rom : roms) {
            if (seen.insert(rom.id).second)
                m_searchLibrary.push_back(std::move(rom));
        }
    }

    std::sort(m_searchLibrary.begin(), m_searchLibrary.end(),
              [](const romm::Rom& a, const romm::Rom& b) {
                  return a.name < b.name;
              });
    m_searchIndexLoaded = m_searchError.empty();
    m_loadingSearch = false;
    applySearch();
}

void BrowseScreen::applySearch() {
    m_searchResults.clear();
    if (m_searchQuery.empty()) {
        m_searchSel = 0;
        m_searchScroll = 0;
        return;
    }

    std::string query = toLower(m_searchQuery);
    for (const auto& rom : m_searchLibrary) {
        if (toLower(rom.name).find(query) != std::string::npos)
            m_searchResults.push_back(rom);
    }
    m_searchSel = 0;
    m_searchScroll = 0;
}

int BrowseScreen::listVisibleRows(int topOffset) const {
    return std::max(1, (CONTENT_H - SECTION_TOP_PAD - topOffset) / LIST_ITEM_H);
}

int BrowseScreen::gridColumns() const {
    return std::max(1, (CONTENT_W + GRID_PAD) / (GRID_CELL_W + GRID_PAD));
}

int BrowseScreen::gridVisibleRows(int topOffset) const {
    int available = CONTENT_H - SECTION_TOP_PAD - topOffset;
    return std::max(1, available / (GRID_CELL_H + GRID_PAD));
}

void BrowseScreen::clampSelection(int& selected, int& scroll, int count, int visible) const {
    if (count <= 0) { selected = 0; scroll = 0; return; }
    if (selected < 0) selected = 0;
    if (selected >= count) selected = count - 1;
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + visible) scroll = selected - visible + 1;
}

void BrowseScreen::clampGridSelection(int& selected, int& scroll, int count, int visibleRows) const {
    if (count <= 0) { selected = 0; scroll = 0; return; }
    if (selected < 0) selected = 0;
    if (selected >= count) selected = count - 1;
    int cols = gridColumns();
    int row = selected / cols;
    if (row < scroll) scroll = row;
    if (row >= scroll + visibleRows) scroll = row - visibleRows + 1;
}

void BrowseScreen::moveHeaderToBody() {
    switch (m_tab) {
    case MainTab::Start:
        m_focus = FocusArea::Start;
        break;
    case MainTab::Platforms:
        m_focus = FocusArea::Platforms;
        break;
    case MainTab::Collections:
        m_focus = FocusArea::Collections;
        break;
    case MainTab::Search:
        m_focus = m_searchResults.empty() ? FocusArea::SearchBox : FocusArea::SearchResults;
        break;
    case MainTab::Queues:
        m_focus = FocusArea::Queues;
        break;
    }
}

void BrowseScreen::switchTab(int delta) {
    if (m_searchEditing) leaveSearchEditing();
    int value = static_cast<int>(m_tab) + delta;
    if (value < 0) value = static_cast<int>(MainTab::Queues);
    if (value > static_cast<int>(MainTab::Queues)) value = 0;
    m_tab = static_cast<MainTab>(value);
    if (m_tab == MainTab::Search) ensureSearchIndex();
    if (m_focus != FocusArea::HeaderSettings)
        moveHeaderToBody();
}

void BrowseScreen::toggleViewMode() {
    m_viewMode = (m_viewMode == ViewMode::List) ? ViewMode::Grid : ViewMode::List;

    if (m_tab == MainTab::Platforms) {
        if (m_platformContextId >= 0) {
            if (m_viewMode == ViewMode::List)
                clampSelection(m_platformGameSel, m_platformGameScroll, static_cast<int>(m_platformGames.size()), listVisibleRows(124));
            else
                clampGridSelection(m_platformGameSel, m_platformGameScroll, static_cast<int>(m_platformGames.size()), gridVisibleRows(124));
        } else if (m_viewMode == ViewMode::List) {
            clampSelection(m_platformSel, m_platformScroll, static_cast<int>(m_platforms.size()), listVisibleRows(80));
        } else {
            clampGridSelection(m_platformSel, m_platformScroll, static_cast<int>(m_platforms.size()), gridVisibleRows(80));
        }
    } else if (m_tab == MainTab::Collections) {
        if (m_collectionContextId >= 0) {
            if (m_viewMode == ViewMode::List)
                clampSelection(m_collectionGameSel, m_collectionGameScroll, static_cast<int>(m_collectionGames.size()), listVisibleRows(124));
            else
                clampGridSelection(m_collectionGameSel, m_collectionGameScroll, static_cast<int>(m_collectionGames.size()), gridVisibleRows(124));
        } else if (m_viewMode == ViewMode::List) {
            clampSelection(m_collectionSel, m_collectionScroll, static_cast<int>(m_collections.size()), listVisibleRows(80));
        } else {
            clampGridSelection(m_collectionSel, m_collectionScroll, static_cast<int>(m_collections.size()), gridVisibleRows(80));
        }
    } else if (m_tab == MainTab::Search) {
        if (m_viewMode == ViewMode::List)
            clampSelection(m_searchSel, m_searchScroll, static_cast<int>(m_searchResults.size()), listVisibleRows(SEARCH_BOX_H + 98));
        else
            clampGridSelection(m_searchSel, m_searchScroll, static_cast<int>(m_searchResults.size()), gridVisibleRows(SEARCH_BOX_H + 98));
    }
}

void BrowseScreen::leaveSearchEditing() {
    m_searchEditing = false;
    SDL_StopTextInput();
}

bool BrowseScreen::clientReady() const {
    return m_client != nullptr && m_loggedIn;
}

void BrowseScreen::handleHeaderTabsInput(SDL_Keycode key) {
    switch (key) {
    case SDLK_LEFT:
        switchTab(-1);
        break;
    case SDLK_RIGHT:
        if (m_tab == MainTab::Queues)
            m_focus = FocusArea::HeaderSettings;
        else
            switchTab(1);
        break;
    case SDLK_DOWN:
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        moveHeaderToBody();
        break;
    default:
        break;
    }
}

void BrowseScreen::handleHeaderSettingsInput(SDL_Keycode key) {
    switch (key) {
    case SDLK_LEFT:
        m_focus = FocusArea::HeaderTabs;
        break;
    case SDLK_DOWN:
        moveHeaderToBody();
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        navigateTo("settings", 0);
        break;
    default:
        break;
    }
}

void BrowseScreen::handleStartInput(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP:
        if (m_startSel == 0) m_focus = FocusArea::HeaderTabs;
        else --m_startSel;
        break;
    case SDLK_DOWN:
        if (m_startSel < 3) ++m_startSel;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        m_tab = static_cast<MainTab>(m_startSel + 1);
        if (m_tab == MainTab::Search) ensureSearchIndex();
        moveHeaderToBody();
        break;
    default:
        break;
    }
}

void BrowseScreen::handlePlatformInput(SDL_Keycode key) {
    if (!clientReady()) {
        if (key == SDLK_UP) m_focus = FocusArea::HeaderTabs;
        return;
    }

    if (m_platformContextId < 0) {
        int count = static_cast<int>(m_platforms.size());
        if (m_viewMode == ViewMode::List) {
            if (key == SDLK_UP) {
                if (m_platformSel == 0) m_focus = FocusArea::HeaderTabs;
                else --m_platformSel;
            } else if (key == SDLK_DOWN) {
                ++m_platformSel;
            } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                loadPlatformGames();
            } else if (key == SDLK_y) {
                toggleViewMode();
                return;
            } else if (key == SDLK_b) {
                m_tab = MainTab::Start;
                m_focus = FocusArea::Start;
                return;
            }
            clampSelection(m_platformSel, m_platformScroll, count, listVisibleRows(80));
            return;
        }

        int cols = gridColumns();
        if (key == SDLK_UP) {
            if (m_platformSel < cols) m_focus = FocusArea::HeaderTabs;
            else m_platformSel -= cols;
        } else if (key == SDLK_DOWN) {
            m_platformSel += cols;
        } else if (key == SDLK_LEFT) {
            --m_platformSel;
        } else if (key == SDLK_RIGHT) {
            ++m_platformSel;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            loadPlatformGames();
        } else if (key == SDLK_y) {
            toggleViewMode();
            return;
        } else if (key == SDLK_b) {
            m_tab = MainTab::Start;
            m_focus = FocusArea::Start;
            return;
        }
        clampGridSelection(m_platformSel, m_platformScroll, count, gridVisibleRows(80));
        return;
    }

    int count = static_cast<int>(m_platformGames.size());
    if (m_viewMode == ViewMode::List) {
        if (key == SDLK_UP) {
            if (m_platformGameSel == 0) m_focus = FocusArea::HeaderTabs;
            else --m_platformGameSel;
        } else if (key == SDLK_DOWN) {
            ++m_platformGameSel;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (!m_platformGames.empty())
                navigateTo("detail", m_platformGames[static_cast<size_t>(m_platformGameSel)].id);
        } else if (key == SDLK_b || key == SDLK_LEFT) {
            m_platformContextId = -1;
            m_platformContextName.clear();
            return;
        } else if (key == SDLK_y) {
            toggleViewMode();
            return;
        }
        clampSelection(m_platformGameSel, m_platformGameScroll, count, listVisibleRows(124));
        return;
    }

    int cols = gridColumns();
    if (key == SDLK_UP) m_platformGameSel -= cols;
    else if (key == SDLK_DOWN) m_platformGameSel += cols;
    else if (key == SDLK_LEFT) {
        if (m_platformGameSel % cols == 0) {
            m_platformContextId = -1;
            m_platformContextName.clear();
            return;
        }
        --m_platformGameSel;
    } else if (key == SDLK_RIGHT) ++m_platformGameSel;
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (!m_platformGames.empty())
            navigateTo("detail", m_platformGames[static_cast<size_t>(m_platformGameSel)].id);
    } else if (key == SDLK_b) {
        m_platformContextId = -1;
        m_platformContextName.clear();
        return;
    } else if (key == SDLK_y) {
        toggleViewMode();
        return;
    }
    clampGridSelection(m_platformGameSel, m_platformGameScroll, count, gridVisibleRows(124));
}

void BrowseScreen::handleCollectionInput(SDL_Keycode key) {
    if (!clientReady()) {
        if (key == SDLK_UP) m_focus = FocusArea::HeaderTabs;
        return;
    }

    if (m_collectionContextId < 0) {
        int count = static_cast<int>(m_collections.size());
        if (m_viewMode == ViewMode::List) {
            if (key == SDLK_UP) {
                if (m_collectionSel == 0) m_focus = FocusArea::HeaderTabs;
                else --m_collectionSel;
            } else if (key == SDLK_DOWN) {
                ++m_collectionSel;
            } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                loadCollectionGames();
            } else if (key == SDLK_y) {
                toggleViewMode();
                return;
            } else if (key == SDLK_b) {
                m_tab = MainTab::Start;
                m_focus = FocusArea::Start;
                return;
            }
            clampSelection(m_collectionSel, m_collectionScroll, count, listVisibleRows(80));
            return;
        }

        int cols = gridColumns();
        if (key == SDLK_UP) {
            if (m_collectionSel < cols) m_focus = FocusArea::HeaderTabs;
            else m_collectionSel -= cols;
        } else if (key == SDLK_DOWN) {
            m_collectionSel += cols;
        } else if (key == SDLK_LEFT) {
            --m_collectionSel;
        } else if (key == SDLK_RIGHT) {
            ++m_collectionSel;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            loadCollectionGames();
        } else if (key == SDLK_y) {
            toggleViewMode();
            return;
        } else if (key == SDLK_b) {
            m_tab = MainTab::Start;
            m_focus = FocusArea::Start;
            return;
        }
        clampGridSelection(m_collectionSel, m_collectionScroll, count, gridVisibleRows(80));
        return;
    }

    int count = static_cast<int>(m_collectionGames.size());
    if (m_viewMode == ViewMode::List) {
        if (key == SDLK_UP) {
            if (m_collectionGameSel == 0) m_focus = FocusArea::HeaderTabs;
            else --m_collectionGameSel;
        } else if (key == SDLK_DOWN) {
            ++m_collectionGameSel;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (!m_collectionGames.empty())
                navigateTo("detail", m_collectionGames[static_cast<size_t>(m_collectionGameSel)].id);
        } else if (key == SDLK_b || key == SDLK_LEFT) {
            m_collectionContextId = -1;
            m_collectionContextName.clear();
            return;
        } else if (key == SDLK_y) {
            toggleViewMode();
            return;
        }
        clampSelection(m_collectionGameSel, m_collectionGameScroll, count, listVisibleRows(124));
        return;
    }

    int cols = gridColumns();
    if (key == SDLK_UP) m_collectionGameSel -= cols;
    else if (key == SDLK_DOWN) m_collectionGameSel += cols;
    else if (key == SDLK_LEFT) {
        if (m_collectionGameSel % cols == 0) {
            m_collectionContextId = -1;
            m_collectionContextName.clear();
            return;
        }
        --m_collectionGameSel;
    } else if (key == SDLK_RIGHT) ++m_collectionGameSel;
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (!m_collectionGames.empty())
            navigateTo("detail", m_collectionGames[static_cast<size_t>(m_collectionGameSel)].id);
    } else if (key == SDLK_b) {
        m_collectionContextId = -1;
        m_collectionContextName.clear();
        return;
    } else if (key == SDLK_y) {
        toggleViewMode();
        return;
    }
    clampGridSelection(m_collectionGameSel, m_collectionGameScroll, count, gridVisibleRows(124));
}

void BrowseScreen::handleSearchBoxInput(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP:
        m_focus = FocusArea::HeaderTabs;
        break;
    case SDLK_DOWN:
        if (!m_searchResults.empty()) m_focus = FocusArea::SearchResults;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_y:
        ensureSearchIndex();
        m_searchEditing = true;
        SDL_StartTextInput();
        break;
    case SDLK_b:
        m_tab = MainTab::Start;
        m_focus = FocusArea::Start;
        break;
    default:
        break;
    }
}

void BrowseScreen::handleSearchResultsInput(SDL_Keycode key) {
    int count = static_cast<int>(m_searchResults.size());
    if (m_viewMode == ViewMode::List) {
        if (key == SDLK_UP) {
            if (m_searchSel == 0) m_focus = FocusArea::SearchBox;
            else --m_searchSel;
        } else if (key == SDLK_DOWN) {
            ++m_searchSel;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (!m_searchResults.empty())
                navigateTo("detail", m_searchResults[static_cast<size_t>(m_searchSel)].id);
        } else if (key == SDLK_LEFT || key == SDLK_b) {
            m_focus = FocusArea::SearchBox;
            return;
        } else if (key == SDLK_y) {
            toggleViewMode();
            return;
        }
        clampSelection(m_searchSel, m_searchScroll, count, listVisibleRows(SEARCH_BOX_H + 98));
        return;
    }

    int cols = gridColumns();
    if (key == SDLK_UP) {
        if (m_searchSel < cols) {
            m_focus = FocusArea::SearchBox;
            return;
        }
        m_searchSel -= cols;
    } else if (key == SDLK_DOWN) m_searchSel += cols;
    else if (key == SDLK_LEFT) {
        if (m_searchSel % cols == 0) {
            m_focus = FocusArea::SearchBox;
            return;
        }
        --m_searchSel;
    } else if (key == SDLK_RIGHT) ++m_searchSel;
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (!m_searchResults.empty())
            navigateTo("detail", m_searchResults[static_cast<size_t>(m_searchSel)].id);
    } else if (key == SDLK_b) {
        m_focus = FocusArea::SearchBox;
        return;
    } else if (key == SDLK_y) {
        toggleViewMode();
        return;
    }
    clampGridSelection(m_searchSel, m_searchScroll, count, gridVisibleRows(SEARCH_BOX_H + 98));
}

void BrowseScreen::handleQueueInput(SDL_Keycode key) {
    auto items = m_downloads.items();
    int count = static_cast<int>(items.size());
    switch (key) {
    case SDLK_UP:
        if (m_queueSel == 0) m_focus = FocusArea::HeaderTabs;
        else --m_queueSel;
        break;
    case SDLK_DOWN:
        ++m_queueSel;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (clientReady() && !items.empty())
            navigateTo("detail", items[static_cast<size_t>(m_queueSel)].romId);
        break;
    case SDLK_b:
        m_tab = MainTab::Start;
        m_focus = FocusArea::Start;
        return;
    default:
        break;
    }
    clampSelection(m_queueSel, m_queueScroll, count, listVisibleRows(80));
}

void BrowseScreen::renderHeader() {
    auto& R = m_renderer;
    R.fillRect(0, 0, SCREEN_W, HEADER_H, Color::Card);
    R.fillRect(0, HEADER_H - 1, SCREEN_W, 1, Color::Separator);

    R.drawText("RomM2Switch", 24, 16, Color::TextWhite, R.fontLarge());

    SDL_Color statusColor = m_loggedIn ? Color::Success : Color::TextDim;
    std::string status = !m_hasConfig
        ? "Setup required"
        : (m_loggedIn ? "Connected" : "Offline");
    R.drawText(status, 250, 22, statusColor, R.fontSmall());

    constexpr int settingsW = 150;
    constexpr int settingsH = 38;
    int settingsX = SCREEN_W - settingsW - 24;
    int settingsY = 16;
    bool settingsFocused = (m_focus == FocusArea::HeaderSettings);
    R.fillRect(settingsX, settingsY, settingsW, settingsH,
               settingsFocused ? Color::CardHover : Color::TabInactive);
    R.drawRect(settingsX, settingsY, settingsW, settingsH,
               settingsFocused ? Color::TextWhite : Color::Separator);
    R.drawTextCentered("Settings", settingsX, settingsY + 8, settingsW,
                       Color::TextWhite, R.fontSmall());

    const std::array<const char*, 5> tabs = {"Start", "Platforms", "Collections", "Search", "Queues"};
    int tabBarX = 24;
    int tabBarY = 58;
    int tabBarW = settingsX - tabBarX - 18;
    int tabW = tabBarW / static_cast<int>(tabs.size());
    R.fillRect(tabBarX, tabBarY, tabBarW, 34, Color::TabInactive);
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        bool active = (i == static_cast<int>(m_tab));
        bool focused = (m_focus == FocusArea::HeaderTabs) && active;
        int x = tabBarX + i * tabW;
        R.fillRect(x, tabBarY, tabW - 2, 34, active ? Color::CardHover : Color::TabInactive);
        if (focused)
            R.drawRect(x, tabBarY, tabW - 2, 34, Color::TextWhite);
        R.drawTextCentered(tabs[static_cast<size_t>(i)], x, tabBarY + 7, tabW - 2,
                           active ? Color::TextWhite : Color::TextDim, R.fontSmall());
    }
}

void BrowseScreen::renderStartTab() {
    auto& R = m_renderer;
    int panelX = CONTENT_X;
    int panelY = CONTENT_Y + SECTION_TOP_PAD;
    int panelW = CONTENT_W;

    R.drawText("Start", panelX, panelY, Color::TextWhite, R.fontLarge());
    R.drawText("Quick access to your library, search, and download queue.",
               panelX, panelY + 34, Color::TextDim, R.fontSmall());

    struct StartRow { const char* title; const char* subtitle; };
    const std::array<StartRow, 4> rows = {{
        {"Platforms", "Browse your available systems"},
        {"Collections", "Open curated RomM collections"},
        {"Search", "Find a game anywhere in the library"},
        {"Queues", "Monitor active and previous downloads"},
    }};

    int cardY = panelY + 84;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        bool selected = (m_focus == FocusArea::Start) && (i == m_startSel);
        int y = cardY + i * 92;
        R.fillRect(panelX, y, panelW, 76, selected ? Color::CardHover : Color::Card);
        R.drawRect(panelX, y, panelW, 76, selected ? Color::TextWhite : Color::Separator);
        R.drawText(rows[static_cast<size_t>(i)].title, panelX + 22, y + 14,
                   Color::TextWhite, R.fontMedium());
        R.drawText(rows[static_cast<size_t>(i)].subtitle, panelX + 22, y + 42,
                   selected ? Color::TextWhite : Color::TextDim, R.fontSmall());

        std::string value;
        if (i == 0) value = std::to_string(m_platforms.size()) + " platforms";
        else if (i == 1) value = std::to_string(m_collections.size()) + " collections";
        else if (i == 2) value = m_searchIndexLoaded ? std::to_string(m_searchLibrary.size()) + " games indexed" : "Build search index";
        else value = std::to_string(m_queueSnapshot.size()) + " tasks";
        int valueW = R.textWidth(value, R.fontSmall());
        R.drawText(value, panelX + panelW - valueW - 24, y + 28,
                   selected ? Color::TextWhite : Color::TextDim, R.fontSmall());
    }

    R.drawText("Recent queue activity", panelX, panelY + 430, Color::TextWhite, R.fontMedium());
    if (m_queueSnapshot.empty()) {
        R.drawText("No downloads queued yet.", panelX, panelY + 464, Color::TextDim, R.fontSmall());
        return;
    }
    for (int i = 0; i < std::min(2, static_cast<int>(m_queueSnapshot.size())); ++i) {
        const auto& item = m_queueSnapshot[static_cast<size_t>(i)];
        int y = panelY + 462 + i * 40;
        R.drawText(item.title, panelX, y, Color::Text, R.fontSmall());
        std::string meta = item.platformName + "  •  " + queueStateLabel(item.state);
        R.drawText(meta, panelX + 360, y, Color::TextDim, R.fontSmall());
    }
}

void BrowseScreen::renderPlatformsTab() {
    auto& R = m_renderer;
    if (!clientReady()) {
        renderDisconnectedState("Platforms", "Open Settings to connect to your RomM server.");
        return;
    }
    if (!m_libraryError.empty()) {
        renderDisconnectedState("Platforms", m_libraryError);
        return;
    }
    if (m_loadingLibrary) {
        R.drawLoadingOverlay("Loading platforms...");
        return;
    }

    if (m_platformContextId < 0) {
        R.drawText("Platforms", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD, Color::TextWhite, R.fontLarge());
        R.drawText("Choose a platform, then open its library.", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 34,
                   Color::TextDim, R.fontSmall());
        if (m_platforms.empty()) {
            R.drawText("No platforms available.", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 80,
                       Color::TextDim, R.fontSmall());
            return;
        }
        if (m_viewMode == ViewMode::List)
            renderCollectionLikeList(m_platforms, m_platformSel, m_platformScroll, m_focus == FocusArea::Platforms);
        else
            renderCollectionLikeGrid(m_platforms, m_platformSel, m_platformScroll, m_focus == FocusArea::Platforms);
        return;
    }

    R.drawText("Platforms / " + m_platformContextName, CONTENT_X, CONTENT_Y + SECTION_TOP_PAD, Color::TextWhite, R.fontLarge());
    R.drawText("B Back to platforms", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 34, Color::TextDim, R.fontSmall());
    if (m_loadingPlatformGames) {
        R.drawLoadingOverlay("Loading games...");
        return;
    }
    if (!m_platformGamesError.empty()) {
        renderDisconnectedState("Platform Library", m_platformGamesError);
        return;
    }
    if (m_platformGames.empty()) {
        R.drawText("No games found.", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 84,
                   Color::TextDim, R.fontSmall());
        return;
    }
    if (m_viewMode == ViewMode::List)
        renderGameList(m_platformGames, m_platformGameSel, m_platformGameScroll, m_focus == FocusArea::Platforms, 44);
    else
        renderGameGrid(m_platformGames, m_platformGameSel, m_platformGameScroll, m_focus == FocusArea::Platforms, 44);
}

void BrowseScreen::renderCollectionsTab() {
    auto& R = m_renderer;
    if (!clientReady()) {
        renderDisconnectedState("Collections", "Open Settings to connect to your RomM server.");
        return;
    }
    if (!m_libraryError.empty()) {
        renderDisconnectedState("Collections", m_libraryError);
        return;
    }
    if (m_loadingLibrary) {
        R.drawLoadingOverlay("Loading collections...");
        return;
    }

    if (m_collectionContextId < 0) {
        R.drawText("Collections", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD, Color::TextWhite, R.fontLarge());
        R.drawText("Choose a collection to open its games.", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 34,
                   Color::TextDim, R.fontSmall());
        if (m_collections.empty()) {
            R.drawText("No collections available.", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 80,
                       Color::TextDim, R.fontSmall());
            return;
        }
        if (m_viewMode == ViewMode::List)
            renderCollectionLikeList(m_collections, m_collectionSel, m_collectionScroll, m_focus == FocusArea::Collections);
        else
            renderCollectionLikeGrid(m_collections, m_collectionSel, m_collectionScroll, m_focus == FocusArea::Collections);
        return;
    }

    R.drawText("Collections / " + m_collectionContextName, CONTENT_X, CONTENT_Y + SECTION_TOP_PAD, Color::TextWhite, R.fontLarge());
    R.drawText("B Back to collections", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 34, Color::TextDim, R.fontSmall());
    if (m_loadingCollectionGames) {
        R.drawLoadingOverlay("Loading games...");
        return;
    }
    if (!m_collectionGamesError.empty()) {
        renderDisconnectedState("Collection Library", m_collectionGamesError);
        return;
    }
    if (m_collectionGames.empty()) {
        R.drawText("No games found.", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 84,
                   Color::TextDim, R.fontSmall());
        return;
    }
    if (m_viewMode == ViewMode::List)
        renderGameList(m_collectionGames, m_collectionGameSel, m_collectionGameScroll, m_focus == FocusArea::Collections, 44);
    else
        renderGameGrid(m_collectionGames, m_collectionGameSel, m_collectionGameScroll, m_focus == FocusArea::Collections, 44);
}

void BrowseScreen::renderSearchTab() {
    auto& R = m_renderer;
    if (!clientReady()) {
        renderDisconnectedState("Search", "Connect to RomM to search your library.");
        return;
    }

    int searchY = CONTENT_Y + SECTION_TOP_PAD;
    R.drawText("Search", CONTENT_X, searchY, Color::TextWhite, R.fontLarge());
    R.drawText("Find games by title across your RomM library.", CONTENT_X, searchY + 34,
               Color::TextDim, R.fontSmall());

    bool boxFocused = (m_focus == FocusArea::SearchBox) || m_searchEditing;
    int boxY = searchY + 72;
    R.fillRect(CONTENT_X, boxY, CONTENT_W, SEARCH_BOX_H, boxFocused ? Color::CardHover : Color::Card);
    R.drawRect(CONTENT_X, boxY, CONTENT_W, SEARCH_BOX_H,
               boxFocused ? Color::TextWhite : Color::Separator);
    std::string query = m_searchQuery.empty() ? "Press A to search by title" : m_searchQuery;
    if (m_searchEditing) query += "|";
    R.drawText(query, CONTENT_X + 18, boxY + 16,
               Color::TextWhite, R.fontMedium());

    if (m_loadingSearch) {
        R.drawLoadingOverlay("Building search index...");
        return;
    }
    if (!m_searchError.empty()) {
        renderDisconnectedState("Search", m_searchError);
        return;
    }
    if (m_searchQuery.empty()) {
        R.drawText("Type a game title to see matching results.", CONTENT_X, boxY + SEARCH_BOX_H + 26,
                   Color::TextDim, R.fontSmall());
        return;
    }
    if (m_searchResults.empty()) {
        R.drawText("No results found.", CONTENT_X, boxY + SEARCH_BOX_H + 26,
                   Color::TextDim, R.fontSmall());
        return;
    }

    if (m_viewMode == ViewMode::List)
        renderGameList(m_searchResults, m_searchSel, m_searchScroll, m_focus == FocusArea::SearchResults, SEARCH_BOX_H + 18);
    else
        renderGameGrid(m_searchResults, m_searchSel, m_searchScroll, m_focus == FocusArea::SearchResults, SEARCH_BOX_H + 18);
}

void BrowseScreen::renderQueuesTab() {
    auto& R = m_renderer;
    R.drawText("Queues", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD, Color::TextWhite, R.fontLarge());
    R.drawText("Active, pending, completed, and failed downloads.", CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 34,
               Color::TextDim, R.fontSmall());
    renderQueueList(m_queueSnapshot);
}

void BrowseScreen::renderDisconnectedState(const std::string& title, const std::string& body) {
    auto& R = m_renderer;
    R.drawText(title, CONTENT_X, CONTENT_Y + SECTION_TOP_PAD, Color::TextWhite, R.fontLarge());
    R.drawText(body, CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 56, Color::TextDim, R.fontSmall());
    if (!m_loginError.empty())
        R.drawText("Last error: " + m_loginError, CONTENT_X, CONTENT_Y + SECTION_TOP_PAD + 92,
                   Color::Error, R.fontSmall());
}

void BrowseScreen::renderCollectionLikeGrid(const std::vector<romm::Platform>& items, int selected, int scroll, bool focused) {
    auto& R = m_renderer;
    int cols = gridColumns();
    int visibleRows = gridVisibleRows(80);
    int startY = CONTENT_Y + SECTION_TOP_PAD + 80;
    int totalRows = (static_cast<int>(items.size()) + cols - 1) / cols;

    for (int row = scroll; row < std::min(scroll + visibleRows + 1, totalRows); ++row) {
        for (int col = 0; col < cols; ++col) {
            int idx = row * cols + col;
            if (idx >= static_cast<int>(items.size())) break;
            int x = CONTENT_X + col * (GRID_CELL_W + GRID_PAD);
            int y = startY + (row - scroll) * (GRID_CELL_H + GRID_PAD);
            bool isSelected = focused && idx == selected;
            const auto& item = items[static_cast<size_t>(idx)];

            R.fillRect(x, y, GRID_CELL_W, GRID_CELL_H, isSelected ? Color::CardHover : Color::Card);
            R.drawRect(x, y, GRID_CELL_W, GRID_CELL_H, isSelected ? Color::TextWhite : Color::Separator);
            R.fillRect(x + 18, y + 18, GRID_CELL_W - 36, GRID_IMG_H - 20, Color::Background);
            R.drawTextCentered(initialsFor(item.name), x, y + 70, GRID_CELL_W,
                               Color::TextWhite, R.fontLarge());
            R.drawTextCentered(truncateText(item.name, GRID_CELL_W - 24, R.fontSmall(), R),
                               x, y + GRID_IMG_H + 12, GRID_CELL_W,
                               Color::TextWhite, R.fontSmall());
            R.drawTextCentered(std::to_string(item.romCount) + " games", x, y + GRID_IMG_H + 42, GRID_CELL_W,
                               Color::TextDim, R.fontSmall());
        }
    }
}

void BrowseScreen::renderCollectionLikeList(const std::vector<romm::Platform>& items, int selected, int scroll, bool focused) {
    auto& R = m_renderer;
    int startY = CONTENT_Y + SECTION_TOP_PAD + 80;
    int visible = listVisibleRows(80);
    int end = std::min(scroll + visible, static_cast<int>(items.size()));
    for (int i = scroll; i < end; ++i) {
        const auto& item = items[static_cast<size_t>(i)];
        int y = startY + (i - scroll) * LIST_ITEM_H;
        bool isSelected = focused && i == selected;
        R.fillRect(CONTENT_X, y, CONTENT_W, LIST_ITEM_H - 4, isSelected ? Color::CardHover : Color::Card);
        R.drawRect(CONTENT_X, y, CONTENT_W, LIST_ITEM_H - 4, isSelected ? Color::TextWhite : Color::Separator);
        R.fillRect(CONTENT_X + 16, y + 12, 52, 48, Color::Background);
        R.drawTextCentered(initialsFor(item.name), CONTENT_X + 16, y + 24, 52, Color::TextWhite, R.fontSmall());
        R.drawText(item.name, CONTENT_X + 90, y + 14, Color::TextWhite, R.fontMedium());
        R.drawText(std::to_string(item.romCount) + " games", CONTENT_X + 90, y + 42, Color::TextDim, R.fontSmall());
    }
}

void BrowseScreen::renderCollectionLikeGrid(const std::vector<romm::Collection>& items, int selected, int scroll, bool focused) {
    auto& R = m_renderer;
    int cols = gridColumns();
    int visibleRows = gridVisibleRows(80);
    int startY = CONTENT_Y + SECTION_TOP_PAD + 80;
    int totalRows = (static_cast<int>(items.size()) + cols - 1) / cols;

    for (int row = scroll; row < std::min(scroll + visibleRows + 1, totalRows); ++row) {
        for (int col = 0; col < cols; ++col) {
            int idx = row * cols + col;
            if (idx >= static_cast<int>(items.size())) break;
            int x = CONTENT_X + col * (GRID_CELL_W + GRID_PAD);
            int y = startY + (row - scroll) * (GRID_CELL_H + GRID_PAD);
            bool isSelected = focused && idx == selected;
            const auto& item = items[static_cast<size_t>(idx)];

            R.fillRect(x, y, GRID_CELL_W, GRID_CELL_H, isSelected ? Color::CardHover : Color::Card);
            R.drawRect(x, y, GRID_CELL_W, GRID_CELL_H, isSelected ? Color::TextWhite : Color::Separator);
            R.fillRect(x + 18, y + 18, GRID_CELL_W - 36, GRID_IMG_H - 20, Color::Background);
            R.drawTextCentered(initialsFor(item.name), x, y + 70, GRID_CELL_W,
                               Color::TextWhite, R.fontLarge());
            R.drawTextCentered(truncateText(item.name, GRID_CELL_W - 24, R.fontSmall(), R),
                               x, y + GRID_IMG_H + 12, GRID_CELL_W,
                               Color::TextWhite, R.fontSmall());
            R.drawTextCentered(std::to_string(item.romCount) + " games", x, y + GRID_IMG_H + 42, GRID_CELL_W,
                               Color::TextDim, R.fontSmall());
        }
    }
}

void BrowseScreen::renderCollectionLikeList(const std::vector<romm::Collection>& items, int selected, int scroll, bool focused) {
    auto& R = m_renderer;
    int startY = CONTENT_Y + SECTION_TOP_PAD + 80;
    int visible = listVisibleRows(80);
    int end = std::min(scroll + visible, static_cast<int>(items.size()));
    for (int i = scroll; i < end; ++i) {
        const auto& item = items[static_cast<size_t>(i)];
        int y = startY + (i - scroll) * LIST_ITEM_H;
        bool isSelected = focused && i == selected;
        R.fillRect(CONTENT_X, y, CONTENT_W, LIST_ITEM_H - 4, isSelected ? Color::CardHover : Color::Card);
        R.drawRect(CONTENT_X, y, CONTENT_W, LIST_ITEM_H - 4, isSelected ? Color::TextWhite : Color::Separator);
        R.fillRect(CONTENT_X + 16, y + 12, 52, 48, Color::Background);
        R.drawTextCentered(initialsFor(item.name), CONTENT_X + 16, y + 24, 52, Color::TextWhite, R.fontSmall());
        R.drawText(item.name, CONTENT_X + 90, y + 14, Color::TextWhite, R.fontMedium());
        R.drawText(std::to_string(item.romCount) + " games", CONTENT_X + 90, y + 42, Color::TextDim, R.fontSmall());
    }
}

void BrowseScreen::renderGameGrid(const std::vector<romm::Rom>& games, int selected, int scroll, bool focused, int topOffset) {
    auto& R = m_renderer;
    int cols = gridColumns();
    int visibleRows = gridVisibleRows(topOffset + 80);
    int totalRows = (static_cast<int>(games.size()) + cols - 1) / cols;
    int startY = CONTENT_Y + SECTION_TOP_PAD + 80 + topOffset;

    for (int row = scroll; row < std::min(scroll + visibleRows + 1, totalRows); ++row) {
        for (int col = 0; col < cols; ++col) {
            int idx = row * cols + col;
            if (idx >= static_cast<int>(games.size())) break;
            const auto& rom = games[static_cast<size_t>(idx)];
            int x = CONTENT_X + col * (GRID_CELL_W + GRID_PAD);
            int y = startY + (row - scroll) * (GRID_CELL_H + GRID_PAD);
            bool isSelected = focused && idx == selected;

            R.fillRect(x, y, GRID_CELL_W, GRID_CELL_H, isSelected ? Color::CardHover : Color::Card);
            R.drawRect(x, y, GRID_CELL_W, GRID_CELL_H, isSelected ? Color::TextWhite : Color::Separator);

            auto it = m_coverCache.find(rom.id);
            if (it != m_coverCache.end() && it->second) {
                R.drawTextureFit(it->second, x + 2, y + 2, GRID_CELL_W - 4, GRID_IMG_H - 4);
            } else {
                R.fillRect(x + 2, y + 2, GRID_CELL_W - 4, GRID_IMG_H - 4, Color::Background);
                R.drawTextCentered("[No Cover]", x, y + GRID_IMG_H / 2 - 16, GRID_CELL_W,
                                   Color::TextDim, R.fontSmall());
            }

            R.drawTextCentered(truncateText(rom.name, GRID_CELL_W - 12, R.fontSmall(), R),
                               x, y + GRID_IMG_H + 10, GRID_CELL_W,
                               Color::TextWhite, R.fontSmall());
            if (!rom.platformName.empty()) {
                R.drawTextCentered(truncateText(rom.platformName, GRID_CELL_W - 12, R.fontSmall(), R),
                                   x, y + GRID_IMG_H + 34, GRID_CELL_W,
                                   Color::TextDim, R.fontSmall());
            }
        }
    }
}

void BrowseScreen::renderGameList(const std::vector<romm::Rom>& games, int selected, int scroll, bool focused, int topOffset) {
    auto& R = m_renderer;
    int startY = CONTENT_Y + SECTION_TOP_PAD + 80 + topOffset;
    int visible = listVisibleRows(topOffset + 80);
    int end = std::min(scroll + visible, static_cast<int>(games.size()));

    for (int i = scroll; i < end; ++i) {
        const auto& rom = games[static_cast<size_t>(i)];
        int y = startY + (i - scroll) * LIST_ITEM_H;
        bool isSelected = focused && i == selected;
        R.fillRect(CONTENT_X, y, CONTENT_W, LIST_ITEM_H - 4, isSelected ? Color::CardHover : Color::Card);
        R.drawRect(CONTENT_X, y, CONTENT_W, LIST_ITEM_H - 4, isSelected ? Color::TextWhite : Color::Separator);

        int textX = CONTENT_X + 20;
        auto it = m_coverCache.find(rom.id);
        if (it != m_coverCache.end() && it->second) {
            R.drawTextureFit(it->second, CONTENT_X + 12, y + 8, 44, 56);
            textX = CONTENT_X + 68;
        }

        R.drawText(rom.name, textX, y + 14, Color::TextWhite, R.fontMedium());
        std::string meta = rom.platformName;
        if (rom.fileSizeBytes > 0) {
            if (!meta.empty()) meta += "  •  ";
            meta += rom.fileSizeStr();
        }
        R.drawText(meta, textX, y + 42, Color::TextDim, R.fontSmall());
    }
}

void BrowseScreen::renderQueueList(const std::vector<QueueItemSnapshot>& items) {
    auto& R = m_renderer;
    int startY = CONTENT_Y + SECTION_TOP_PAD + 80;
    int visible = listVisibleRows(80);
    if (items.empty()) {
        R.drawText("No queued downloads yet.", CONTENT_X, startY,
                   Color::TextDim, R.fontSmall());
        return;
    }

    int end = std::min(m_queueScroll + visible, static_cast<int>(items.size()));
    for (int i = m_queueScroll; i < end; ++i) {
        const auto& item = items[static_cast<size_t>(i)];
        int y = startY + (i - m_queueScroll) * LIST_ITEM_H;
        bool selected = (m_focus == FocusArea::Queues) && (i == m_queueSel);
        R.fillRect(CONTENT_X, y, CONTENT_W, LIST_ITEM_H - 4, selected ? Color::CardHover : Color::Card);
        R.drawRect(CONTENT_X, y, CONTENT_W, LIST_ITEM_H - 4, selected ? Color::TextWhite : Color::Separator);

        int textX = CONTENT_X + 20;
        auto it = m_coverCache.find(item.romId);
        if (it != m_coverCache.end() && it->second) {
            R.drawTextureFit(it->second, CONTENT_X + 12, y + 8, 44, 56);
            textX = CONTENT_X + 68;
        }

        R.drawText(item.title, textX, y + 10, Color::TextWhite, R.fontSmall());
        std::string state = std::string(queueStateLabel(item.state)) + "  •  " + item.platformName;
        R.drawText(state, textX, y + 32, Color::TextDim, R.fontSmall());

        std::string extra;
        if (item.state == QueueItemState::Downloading) {
            extra = formatBytes(item.bytesReceived) + " / " + formatBytes(std::max(1LL, item.bytesTotal));
            if (item.speedBytesPerSec > 0)
                extra += "  •  " + formatBytes(item.speedBytesPerSec) + "/s";
            if (item.etaSeconds >= 0)
                extra += "  •  ETA " + formatEta(item.etaSeconds);
        } else if (item.state == QueueItemState::Failed && !item.error.empty()) {
            extra = item.error;
        } else {
            extra = item.destPath;
        }
        R.drawText(truncateText(extra, CONTENT_W - (textX - CONTENT_X) - 18, R.fontSmall(), R),
                   textX, y + 50, selected ? Color::TextWhite : Color::TextDim, R.fontSmall());
    }
}

void BrowseScreen::requestVisibleCovers() {
    if (m_tab == MainTab::Platforms && m_platformContextId >= 0) {
        if (m_viewMode == ViewMode::List) {
            int end = std::min(m_platformGameScroll + listVisibleRows(124), static_cast<int>(m_platformGames.size()));
            for (int i = m_platformGameScroll; i < end; ++i)
                requestCover(m_platformGames[static_cast<size_t>(i)].id, m_platformGames[static_cast<size_t>(i)].coverPathSmall);
        } else {
            int cols = gridColumns();
            int rows = gridVisibleRows(124);
            for (int row = m_platformGameScroll; row < m_platformGameScroll + rows + 1; ++row)
                for (int col = 0; col < cols; ++col) {
                    int idx = row * cols + col;
                    if (idx < static_cast<int>(m_platformGames.size()))
                        requestCover(m_platformGames[static_cast<size_t>(idx)].id, m_platformGames[static_cast<size_t>(idx)].coverPathSmall);
                }
        }
    } else if (m_tab == MainTab::Collections && m_collectionContextId >= 0) {
        if (m_viewMode == ViewMode::List) {
            int end = std::min(m_collectionGameScroll + listVisibleRows(124), static_cast<int>(m_collectionGames.size()));
            for (int i = m_collectionGameScroll; i < end; ++i)
                requestCover(m_collectionGames[static_cast<size_t>(i)].id, m_collectionGames[static_cast<size_t>(i)].coverPathSmall);
        } else {
            int cols = gridColumns();
            int rows = gridVisibleRows(124);
            for (int row = m_collectionGameScroll; row < m_collectionGameScroll + rows + 1; ++row)
                for (int col = 0; col < cols; ++col) {
                    int idx = row * cols + col;
                    if (idx < static_cast<int>(m_collectionGames.size()))
                        requestCover(m_collectionGames[static_cast<size_t>(idx)].id, m_collectionGames[static_cast<size_t>(idx)].coverPathSmall);
                }
        }
    } else if (m_tab == MainTab::Search) {
        if (m_viewMode == ViewMode::List) {
            int end = std::min(m_searchScroll + listVisibleRows(SEARCH_BOX_H + 98), static_cast<int>(m_searchResults.size()));
            for (int i = m_searchScroll; i < end; ++i)
                requestCover(m_searchResults[static_cast<size_t>(i)].id, m_searchResults[static_cast<size_t>(i)].coverPathSmall);
        } else {
            int cols = gridColumns();
            int rows = gridVisibleRows(SEARCH_BOX_H + 98);
            for (int row = m_searchScroll; row < m_searchScroll + rows + 1; ++row)
                for (int col = 0; col < cols; ++col) {
                    int idx = row * cols + col;
                    if (idx < static_cast<int>(m_searchResults.size()))
                        requestCover(m_searchResults[static_cast<size_t>(idx)].id, m_searchResults[static_cast<size_t>(idx)].coverPathSmall);
                }
        }
    } else if (m_tab == MainTab::Queues) {
        int end = std::min(m_queueScroll + listVisibleRows(80), static_cast<int>(m_queueSnapshot.size()));
        for (int i = m_queueScroll; i < end; ++i)
            requestCover(m_queueSnapshot[static_cast<size_t>(i)].romId, m_queueSnapshot[static_cast<size_t>(i)].coverPathSmall);
    }
}

void BrowseScreen::requestCover(int romId, const std::string& path) {
    if (path.empty()) return;
    std::lock_guard<std::mutex> lock(m_coverMutex);
    if (m_coverCache.count(romId) || m_coverRequested.count(romId)) return;
    m_coverRequested.insert(romId);
    m_coverQueue.push_back({romId, path});
}

void BrowseScreen::processCoverResults() {
    std::lock_guard<std::mutex> lock(m_coverMutex);
    while (!m_coverResults.empty()) {
        auto result = std::move(m_coverResults.front());
        m_coverResults.pop_front();
        if (m_coverCache.count(result.romId) == 0) {
            SDL_Texture* tex = m_renderer.loadTextureFromMemory(result.data);
            if (tex) m_coverCache[result.romId] = tex;
        }
    }
}

void BrowseScreen::coverWorker() {
    while (!m_coverStop.load()) {
        CoverRequest request;
        {
            std::lock_guard<std::mutex> lock(m_coverMutex);
            if (m_coverQueue.empty()) {
                request.romId = -1;
            } else {
                request = std::move(m_coverQueue.front());
                m_coverQueue.pop_front();
            }
        }

        if (request.romId < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }
        if (!clientReady()) continue;

        std::string error;
        auto data = m_client->fetchCoverData(request.coverPath, error);
        if (!data.empty()) {
            std::lock_guard<std::mutex> lock(m_coverMutex);
            m_coverResults.push_back({request.romId, std::move(data)});
        }
    }
}

void BrowseScreen::clearCovers() {
    stopCoverThread();
    for (auto& [id, tex] : m_coverCache) {
        if (tex) SDL_DestroyTexture(tex);
    }
    m_coverCache.clear();
    m_coverRequested.clear();
    {
        std::lock_guard<std::mutex> lock(m_coverMutex);
        m_coverQueue.clear();
        m_coverResults.clear();
    }
    m_coverStop = false;
    m_coverThread = std::thread(&BrowseScreen::coverWorker, this);
}

void BrowseScreen::stopCoverThread() {
    m_coverStop = true;
    if (m_coverThread.joinable())
        m_coverThread.join();
}
