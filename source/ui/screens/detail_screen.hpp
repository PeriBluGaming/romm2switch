#pragma once

#include "ui/renderer.hpp"
#include "ui/download_queue.hpp"
#include "ui/screens/screen.hpp"
#include "models/models.hpp"
#include "api/romm_client.hpp"
#include "config.hpp"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DetailScreen — ROM details + cover image + download button
// ---------------------------------------------------------------------------
class DetailScreen : public Screen {
public:
    DetailScreen(Renderer& renderer, NavigateFn navigate,
                 romm::RommClient& client, const romm::Config& config,
                 DownloadQueue& downloads,
                 int romId);
    ~DetailScreen() override;

    void onEnter() override;
    bool update(const SDL_Event& event) override;
    void render() override;

private:
    romm::RommClient& m_client;
    romm::Config      m_config;
    DownloadQueue&    m_downloads;
    int               m_romId;
    romm::Rom         m_rom;

    bool        m_loading = true;
    std::string m_error;

    // Cover image
    SDL_Texture* m_coverTex = nullptr;

    // Download state
    enum class DownloadState { Idle, Queued };
    DownloadState          m_dlState   = DownloadState::Idle;
    std::string            m_dlStatus;

    static constexpr int HEADER_H  = 60;
    static constexpr int STATUS_H  = 44;
    static constexpr int CONTENT_Y = HEADER_H;

    // Cover image dimensions
    static constexpr int COVER_W   = 200;
    static constexpr int COVER_H   = 280;
    static constexpr int COVER_PAD = 20;

    void loadData();
    void loadCover();
    void startDownload();
    std::string buildDestPath() const;

    // Wrap text to multiple lines given a max pixel width
    std::vector<std::string> wrapText(const std::string& text, int maxWidth,
                                      TTF_Font* font) const;
};
