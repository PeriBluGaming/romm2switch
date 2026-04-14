#pragma once

#include "ui/renderer.hpp"
#include "ui/screens/screen.hpp"
#include "models/models.hpp"
#include "api/romm_client.hpp"

#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// CollectionScreen — scrollable list of collections
// ---------------------------------------------------------------------------
class CollectionScreen : public Screen {
public:
    CollectionScreen(Renderer& renderer, NavigateFn navigate,
                     romm::RommClient& client);

    void onEnter() override;
    bool update(const SDL_Event& event) override;
    void render() override;

private:
    romm::RommClient&              m_client;
    std::vector<romm::Collection>  m_collections;
    int                            m_selected  = 0;
    int                            m_scrollOff = 0;
    bool                           m_loading   = true;
    std::string                    m_error;

    static constexpr int ITEM_H       = 60;
    static constexpr int HEADER_H     = 60;
    static constexpr int STATUS_H     = 44;
    static constexpr int CONTENT_Y    = HEADER_H;
    static constexpr int CONTENT_H    = SCREEN_H - HEADER_H - STATUS_H;
    static constexpr int VISIBLE_ROWS = CONTENT_H / ITEM_H;

    void loadData();
    void clampScroll();
};
