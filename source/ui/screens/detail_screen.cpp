#include "ui/screens/detail_screen.hpp"
#include "ui/renderer.hpp"

#include <algorithm>
#include <sstream>

DetailScreen::DetailScreen(Renderer& renderer, NavigateFn navigate,
                           romm::RommClient& client, const romm::Config& config,
                           int romId)
    : Screen(renderer, std::move(navigate))
    , m_client(client)
    , m_config(config)
    , m_romId(romId)
{}

void DetailScreen::onEnter() {
    m_loading  = true;
    m_error.clear();
    m_dlState  = DownloadState::Idle;
    m_dlError.clear();
    m_dlRecv   = 0;
    m_dlTotal  = 1;
    loadData();
}

void DetailScreen::loadData() {
    m_rom     = m_client.getRom(m_romId, m_error);
    m_loading = false;
}

std::string DetailScreen::buildDestPath() const {
    std::string path = m_config.downloadPath;
    if (!path.empty() && path.back() != '/') path += '/';
    if (!m_rom.platformSlug.empty())
        path += m_rom.platformSlug + '/';
    else if (!m_rom.platformName.empty())
        path += m_rom.platformName + '/';
    path += m_rom.fileName;
    return path;
}

void DetailScreen::startDownload() {
    if (m_dlState == DownloadState::Downloading) return;
    if (m_rom.fileName.empty()) {
        m_dlError = "No file name available for this ROM.";
        m_dlState = DownloadState::Failed;
        return;
    }

    m_dlDestPath = buildDestPath();
    m_dlState    = DownloadState::Downloading;
    m_dlRecv     = 0;
    m_dlTotal    = (m_rom.fileSizeBytes > 0) ? m_rom.fileSizeBytes : 1;
    m_dlCancel   = false;

    // Join any previous thread
    if (m_dlThread.joinable()) m_dlThread.join();

    m_dlThread = std::thread([this]() {
        std::string err;
        bool ok = m_client.downloadRom(
            m_rom, m_dlDestPath,
            [this](long long recv, long long total) {
                m_dlRecv  = recv;
                m_dlTotal = (total > 0) ? total : 1;
            },
            m_dlCancel,
            err);

        if (ok) {
            m_dlState = DownloadState::Done;
        } else {
            m_dlError = err;
            m_dlState = DownloadState::Failed;
        }
    });
}

std::vector<std::string> DetailScreen::wrapText(const std::string& text,
                                                 int maxWidth,
                                                 TTF_Font* font) const {
    std::vector<std::string> lines;
    if (text.empty()) return lines;

    std::istringstream words(text);
    std::string word, line;
    while (words >> word) {
        std::string candidate = line.empty() ? word : line + ' ' + word;
        int w = 0;
        TTF_SizeUTF8(font, candidate.c_str(), &w, nullptr);
        if (w <= maxWidth) {
            line = candidate;
        } else {
            if (!line.empty()) lines.push_back(line);
            line = word;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

bool DetailScreen::update(const SDL_Event& event) {
    if (m_loading) return true;

    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_x:
        case SDLK_X:
            if (m_dlState == DownloadState::Idle ||
                m_dlState == DownloadState::Failed ||
                m_dlState == DownloadState::Done) {
                startDownload();
            }
            break;
        case SDLK_b:
        case SDLK_b:
            // Signal download cancellation and wait for the thread to finish
            // before navigating away to avoid dangling references.
            if (m_dlState == DownloadState::Downloading) {
                m_dlCancel = true;
            }
            if (m_dlThread.joinable()) m_dlThread.join();
            navigateTo("back", 0);
            break;
        default: break;
        }
    }
    return true;
}

void DetailScreen::render() {
    auto& R = m_renderer;
    R.fillRect(0, 0, SCREEN_W, SCREEN_H, Color::Background);

    if (m_loading) {
        R.drawHeader("ROM Details");
        R.drawLoadingOverlay("Loading ROM info...");
        R.drawStatusBar("");
        return;
    }

    if (!m_error.empty()) {
        R.drawHeader("ROM Details");
        R.drawErrorScreen(m_error);
        R.drawStatusBar("B Back");
        return;
    }

    R.drawHeader(m_rom.name);

    int cx = 30, cy = CONTENT_Y + 20;
    const int LABEL_W = 220;

    // ROM name (large)
    R.drawText(m_rom.name, cx, cy, Color::TextWhite, R.fontLarge());
    cy += 46;

    // Platform
    R.drawText("Platform:", cx, cy, Color::TextDim, R.fontSmall());
    R.drawText(m_rom.platformName, cx + LABEL_W, cy, Color::Text, R.fontSmall());
    cy += 28;

    // File name
    R.drawText("File:", cx, cy, Color::TextDim, R.fontSmall());
    R.drawText(m_rom.fileName, cx + LABEL_W, cy, Color::Text, R.fontSmall());
    cy += 28;

    // File size
    R.drawText("Size:", cx, cy, Color::TextDim, R.fontSmall());
    R.drawText(m_rom.fileSizeStr(), cx + LABEL_W, cy, Color::Text, R.fontSmall());
    cy += 28;

    // Regions
    if (!m_rom.regions.empty()) {
        std::string regions;
        for (size_t i = 0; i < m_rom.regions.size(); ++i) {
            if (i > 0) regions += ", ";
            regions += m_rom.regions[i];
        }
        R.drawText("Region:", cx, cy, Color::TextDim, R.fontSmall());
        R.drawText(regions, cx + LABEL_W, cy, Color::Text, R.fontSmall());
        cy += 28;
    }

    cy += 16;
    R.fillRect(cx, cy, SCREEN_W - cx * 2, 1, Color::Separator);
    cy += 16;

    // Summary (word-wrapped)
    if (!m_rom.summary.empty()) {
        auto lines = wrapText(m_rom.summary, SCREEN_W - cx * 2 - 40, R.fontSmall());
        int maxLines = (SCREEN_H - STATUS_H - cy - 120) / 22;
        for (int i = 0; i < std::min(static_cast<int>(lines.size()), maxLines); ++i) {
            R.drawText(lines[static_cast<size_t>(i)], cx, cy, Color::TextDim, R.fontSmall());
            cy += 22;
        }
    }

    // -----------------------------------------------------------------------
    // Download area (bottom section, above status bar)
    // -----------------------------------------------------------------------
    int dlY = SCREEN_H - STATUS_H - 90;
    R.fillRect(0, dlY - 8, SCREEN_W, 1, Color::Separator);

    switch (m_dlState) {
    case DownloadState::Idle:
    {
        // Draw [X] Download button
        int bw = 300, bh = 46;
        int bx = (SCREEN_W - bw) / 2;
        int by = dlY + 8;
        R.fillRect(bx, by, bw, bh, Color::CardHover);
        R.drawTextCentered("\xe2\xac\x87  Download", bx, by + (bh - 26) / 2, bw,
                           Color::TextWhite, R.fontMedium());
        break;
    }
    case DownloadState::Downloading:
    {
        long long recv  = m_dlRecv.load();
        long long total = m_dlTotal.load();
        float     pct   = (total > 0) ? static_cast<float>(recv) / total : 0.0f;

        R.drawText("Downloading...", 30, dlY + 8, Color::TextWhite);

        // Build progress string
        auto fmtSize = [](long long b) -> std::string {
            if (b < 1024)          return std::to_string(b) + " B";
            if (b < 1024*1024)     return std::to_string(b/1024) + " KB";
            if (b < 1024LL*1024*1024) return std::to_string(b/(1024*1024)) + " MB";
            return std::to_string(b/(1024LL*1024*1024)) + " GB";
        };
        std::string progress = fmtSize(recv) + " / " + fmtSize(total);
        int pw = R.textWidth(progress, R.fontSmall());
        R.drawText(progress, SCREEN_W - pw - 30, dlY + 10,
                   Color::TextDim, R.fontSmall());

        R.drawProgressBar(30, dlY + 40, SCREEN_W - 60, 16, pct);
        break;
    }
    case DownloadState::Done:
    {
        R.drawTextCentered("\xe2\x9c\x94 Download complete!", 0, dlY + 20,
                           SCREEN_W, Color::Success, R.fontMedium());
        // Show destination
        R.drawTextCentered(m_dlDestPath, 0, dlY + 50, SCREEN_W,
                           Color::TextDim, R.fontSmall());
        break;
    }
    case DownloadState::Failed:
    {
        R.drawTextCentered("\xe2\x9c\x98 Download failed: " + m_dlError,
                           0, dlY + 10, SCREEN_W, Color::Error, R.fontSmall());
        int bw = 280, bh = 38;
        int bx = (SCREEN_W - bw) / 2;
        int by = dlY + 42;
        R.fillRect(bx, by, bw, bh, Color::Card);
        R.drawRect(bx, by, bw, bh, Color::CardHover);
        R.drawTextCentered("Retry (X)", bx, by + (bh - 20) / 2, bw,
                           Color::TextWhite, R.fontSmall());
        break;
    }
    }

    // Status bar
    std::string hint;
    if (m_dlState == DownloadState::Idle || m_dlState == DownloadState::Failed)
        hint = "X Download  B Back";
    else if (m_dlState == DownloadState::Done)
        hint = "X Download Again  B Back";
    else
        hint = "Downloading...  B Cancel & Back";
    R.drawStatusBar(hint);
}
