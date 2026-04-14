#pragma once

#include "ui/renderer.hpp"
#include "ui/screens/screen.hpp"
#include "models/models.hpp"
#include "api/romm_client.hpp"
#include "config.hpp"

#include <atomic>
#include <thread>
#include <string>

// ---------------------------------------------------------------------------
// DetailScreen — ROM details + download button
// ---------------------------------------------------------------------------
class DetailScreen : public Screen {
public:
    DetailScreen(Renderer& renderer, NavigateFn navigate,
                 romm::RommClient& client, const romm::Config& config,
                 int romId);

    void onEnter() override;
    bool update(const SDL_Event& event) override;
    void render() override;

private:
    romm::RommClient& m_client;
    romm::Config      m_config;
    int               m_romId;
    romm::Rom         m_rom;

    bool        m_loading = true;
    std::string m_error;

    // Download state
    enum class DownloadState { Idle, Downloading, Done, Failed };
    DownloadState          m_dlState   = DownloadState::Idle;
    std::atomic<long long> m_dlRecv{0};
    std::atomic<long long> m_dlTotal{1};
    std::atomic<bool>      m_dlCancel{false};
    std::string            m_dlError;
    std::thread            m_dlThread;
    std::string            m_dlDestPath;

    static constexpr int HEADER_H  = 60;
    static constexpr int STATUS_H  = 44;
    static constexpr int CONTENT_Y = HEADER_H;

    void loadData();
    void startDownload();
    std::string buildDestPath() const;

    // Wrap text to multiple lines given a max pixel width
    std::vector<std::string> wrapText(const std::string& text, int maxWidth,
                                      TTF_Font* font) const;
};
