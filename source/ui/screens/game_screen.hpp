#pragma once

#include "ui/renderer.hpp"
#include "ui/screens/screen.hpp"
#include "models/models.hpp"
#include "api/romm_client.hpp"

#include <vector>
#include <string>

// Mode determines which API call fetches the game list
enum class GameListMode { Platform, Collection };

// ---------------------------------------------------------------------------
// GameScreen — scrollable list of ROMs for a platform or collection
// ---------------------------------------------------------------------------
class GameScreen : public Screen {
public:
    GameScreen(Renderer& renderer, NavigateFn navigate,
               romm::RommClient& client, int contextId, GameListMode mode);

    void onEnter() override;
    bool update(const SDL_Event& event) override;
    void render() override;

private:
    romm::RommClient&     m_client;
    int                   m_contextId;
    GameListMode          m_mode;
    std::vector<romm::Rom> m_roms;
    int                   m_selected  = 0;
    int                   m_scrollOff = 0;
    bool                  m_loading   = true;
    std::string           m_error;
    std::string           m_title;

    // Search / filter
    std::string m_filter;
    bool        m_filterMode = false;
    std::vector<romm::Rom> m_filtered;

    static constexpr int ITEM_H       = 55;
    static constexpr int HEADER_H     = 60;
    static constexpr int STATUS_H     = 44;
    static constexpr int CONTENT_Y    = HEADER_H;
    static constexpr int CONTENT_H    = SCREEN_H - HEADER_H - STATUS_H;
    static constexpr int VISIBLE_ROWS = CONTENT_H / ITEM_H;

    void loadData();
    void clampScroll();
    void applyFilter();
    const std::vector<romm::Rom>& displayList() const;
};
