#include "ui/screens/detail_screen.hpp"
#include "ui/renderer.hpp"

#include <algorithm>
#include <sstream>

DetailScreen::DetailScreen(Renderer& renderer, NavigateFn navigate,
                           romm::RommClient& client, const romm::Config& config,
                           DownloadQueue& downloads,
                           int romId)
    : Screen(renderer, std::move(navigate))
    , m_client(client)
    , m_config(config)
    , m_downloads(downloads)
    , m_romId(romId)
{}

DetailScreen::~DetailScreen() {
    if (m_coverTex) {
        SDL_DestroyTexture(m_coverTex);
        m_coverTex = nullptr;
    }
}

void DetailScreen::onEnter() {
    m_loading  = true;
    m_error.clear();
    m_dlState  = DownloadState::Idle;
    m_dlStatus.clear();
    if (m_coverTex) {
        SDL_DestroyTexture(m_coverTex);
        m_coverTex = nullptr;
    }
    loadData();
}

void DetailScreen::loadData() {
    m_rom     = m_client.getRom(m_romId, m_error);
    m_loading = false;
    loadCover();
}

void DetailScreen::loadCover() {
    if (!m_rom.hasCover()) return;
    std::string err;
    auto data = m_client.fetchCoverData(m_rom.coverPathSmall, err);
    if (!data.empty()) {
        m_coverTex = m_renderer.loadTextureFromMemory(data);
    }
}

std::string DetailScreen::buildDestPath() const {
    std::string path = m_config.downloadPath;
    if (!path.empty() && path.back() != '/') path += '/';
    if (!m_rom.platformFsSlug.empty())
        path += m_rom.platformFsSlug + '/';
    else if (!m_rom.platformSlug.empty())
        path += m_rom.platformSlug + '/';
    else if (!m_rom.platformName.empty())
        path += m_rom.platformName + '/';
    path += m_rom.fileName;
    return path;
}

void DetailScreen::startDownload() {
    if (m_rom.fileName.empty()) {
        m_dlStatus = "No file name available for this ROM.";
        return;
    }

    m_downloads.enqueue(m_rom, buildDestPath());
    m_dlState  = DownloadState::Queued;
    m_dlStatus = "Added to queue. Open the Queues tab to follow progress.";
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
            if (m_dlState == DownloadState::Idle) {
                startDownload();
            }
            break;
        case SDLK_b:
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

    // If cover is available, draw it on the left and shift metadata to the right
    int metaX = cx;
    if (m_coverTex) {
        R.drawTextureFit(m_coverTex, cx, cy, COVER_W, COVER_H);
        metaX = cx + COVER_W + COVER_PAD;
    }

    // ROM name (large)
    R.drawText(m_rom.name, metaX, cy, Color::TextWhite, R.fontLarge());
    cy += 46;

    // Platform
    R.drawText("Platform:", metaX, cy, Color::TextDim, R.fontSmall());
    R.drawText(m_rom.platformName, metaX + LABEL_W, cy, Color::Text, R.fontSmall());
    cy += 28;

    // File name
    R.drawText("File:", metaX, cy, Color::TextDim, R.fontSmall());
    R.drawText(m_rom.fileName, metaX + LABEL_W, cy, Color::Text, R.fontSmall());
    cy += 28;

    // File size
    R.drawText("Size:", metaX, cy, Color::TextDim, R.fontSmall());
    R.drawText(m_rom.fileSizeStr(), metaX + LABEL_W, cy, Color::Text, R.fontSmall());
    cy += 28;

    // Regions
    if (!m_rom.regions.empty()) {
        std::string regions;
        for (size_t i = 0; i < m_rom.regions.size(); ++i) {
            if (i > 0) regions += ", ";
            regions += m_rom.regions[i];
        }
        R.drawText("Region:", metaX, cy, Color::TextDim, R.fontSmall());
        R.drawText(regions, metaX + LABEL_W, cy, Color::Text, R.fontSmall());
        cy += 28;
    }

    // Ensure cy is below the cover image if present
    if (m_coverTex) {
        int coverBottom = CONTENT_Y + 20 + COVER_H;
        if (cy < coverBottom) cy = coverBottom;
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
        int bw = 300, bh = 46;
        int bx = (SCREEN_W - bw) / 2;
        int by = dlY + 8;
        R.fillRect(bx, by, bw, bh, Color::CardHover);
        R.drawTextCentered("Add to Queue", bx, by + (bh - 26) / 2, bw,
                           Color::TextWhite, R.fontMedium());
        if (!m_dlStatus.empty()) {
            R.drawTextCentered(m_dlStatus, 0, dlY + 60, SCREEN_W,
                               Color::Error, R.fontSmall());
        }
        break;
    }
    case DownloadState::Queued:
    {
        R.drawTextCentered("Download queued", 0, dlY + 12, SCREEN_W,
                           Color::Success, R.fontMedium());
        R.drawTextCentered(m_dlStatus, 0, dlY + 46, SCREEN_W,
                           Color::TextDim, R.fontSmall());
        break;
    }
    }

    // Status bar
    std::string hint;
    if (m_dlState == DownloadState::Idle)
        hint = "X Queue Download  B Back";
    else
        hint = "Queued  B Back";
    R.drawStatusBar(hint);
}
