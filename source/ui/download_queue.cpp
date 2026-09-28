#include "ui/download_queue.hpp"

#include "api/romm_client.hpp"

#include <algorithm>
#include <chrono>

DownloadQueue::DownloadQueue(const romm::Config& config)
    : m_config(config)
    , m_worker(&DownloadQueue::workerLoop, this)
{}

DownloadQueue::~DownloadQueue() {
    m_stop = true;
    if (m_worker.joinable()) m_worker.join();
}

void DownloadQueue::updateConfig(const romm::Config& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

void DownloadQueue::enqueue(const romm::Rom& rom, const std::string& destPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    QueueItem item;
    item.romId          = rom.id;
    item.title          = rom.name;
    item.platformName   = rom.platformName;
    item.fileName       = rom.fileName;
    item.coverPathSmall = rom.coverPathSmall;
    item.destPath       = destPath;
    item.bytesTotal     = rom.fileSizeBytes;
    m_items.push_back(std::move(item));
}

std::vector<QueueItemSnapshot> DownloadQueue::items() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<QueueItemSnapshot> out;
    out.reserve(m_items.size());
    for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
        QueueItemSnapshot snap;
        snap.romId            = it->romId;
        snap.title            = it->title;
        snap.platformName     = it->platformName;
        snap.fileName         = it->fileName;
        snap.coverPathSmall   = it->coverPathSmall;
        snap.destPath         = it->destPath;
        snap.error            = it->error;
        snap.state            = it->state;
        snap.bytesReceived    = it->bytesReceived;
        snap.bytesTotal       = it->bytesTotal;
        snap.speedBytesPerSec = it->speedBytesPerSec;
        snap.etaSeconds       = it->etaSeconds;
        out.push_back(std::move(snap));
    }
    return out;
}

void DownloadQueue::workerLoop() {
    while (!m_stop.load()) {
        int index = -1;
        romm::Config config;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (size_t i = 0; i < m_items.size(); ++i) {
                if (m_items[i].state == QueueItemState::Queued) {
                    index = static_cast<int>(i);
                    m_items[i].state = QueueItemState::Downloading;
                    m_items[i].bytesReceived = 0;
                    m_items[i].speedBytesPerSec = 0;
                    m_items[i].etaSeconds = -1;
                    config = m_config;
                    break;
                }
            }
        }

        if (index < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            continue;
        }

        romm::RommClient client(config);
        std::string error;
        auto lastTick = std::chrono::steady_clock::now();
        long long lastBytes = 0;
        QueueItemSnapshot current;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            current.romId          = m_items[static_cast<size_t>(index)].romId;
            current.title          = m_items[static_cast<size_t>(index)].title;
            current.platformName   = m_items[static_cast<size_t>(index)].platformName;
            current.fileName       = m_items[static_cast<size_t>(index)].fileName;
            current.coverPathSmall = m_items[static_cast<size_t>(index)].coverPathSmall;
        }

        romm::Rom rom;
        rom.id             = current.romId;
        rom.name           = current.title;
        rom.platformName   = current.platformName;
        rom.fileName       = current.fileName;
        rom.coverPathSmall = current.coverPathSmall;

        const std::string destPath = [&]() {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_items[static_cast<size_t>(index)].destPath;
        }();

        bool ok = client.downloadRom(
            rom, destPath,
            [this, index, &lastTick, &lastBytes](long long recv, long long total) {
                auto now = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count();
                long long speed = 0;
                if (ms > 0) {
                    speed = (recv - lastBytes) * 1000 / ms;
                }
                lastTick = now;
                lastBytes = recv;

                std::lock_guard<std::mutex> lock(m_mutex);
                auto& item = m_items[static_cast<size_t>(index)];
                item.bytesReceived = recv;
                item.bytesTotal = total;
                item.speedBytesPerSec = std::max(0LL, speed);
                if (item.speedBytesPerSec > 0 && total > recv)
                    item.etaSeconds = static_cast<int>((total - recv) / item.speedBytesPerSec);
                else
                    item.etaSeconds = -1;
            },
            m_stop,
            error);

        std::lock_guard<std::mutex> lock(m_mutex);
        auto& item = m_items[static_cast<size_t>(index)];
        if (m_stop.load()) {
            item.state = QueueItemState::Cancelled;
            item.error.clear();
        } else if (ok) {
            item.state = QueueItemState::Completed;
            item.bytesReceived = item.bytesTotal;
            item.speedBytesPerSec = 0;
            item.etaSeconds = 0;
            item.error.clear();
        } else {
            item.state = QueueItemState::Failed;
            item.speedBytesPerSec = 0;
            item.etaSeconds = -1;
            item.error = error;
        }
    }
}
