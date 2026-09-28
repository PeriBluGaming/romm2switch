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
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& item : m_items) {
            if (item.cancelFlag) item.cancelFlag->store(true);
        }
    }
    if (m_worker.joinable()) m_worker.join();
}

void DownloadQueue::updateConfig(const romm::Config& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

void DownloadQueue::enqueue(const romm::Rom& rom, const std::string& destPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    QueueItem item;
    item.taskId         = m_nextTaskId++;
    item.romId          = rom.id;
    item.title          = rom.name;
    item.platformName   = rom.platformName;
    item.fileName       = rom.fileName;
    item.coverPathSmall = rom.coverPathSmall;
    item.destPath       = destPath;
    item.bytesTotal     = rom.fileSizeBytes;
    item.fileSizeBytes  = rom.fileSizeBytes;
    item.cancelFlag     = std::make_shared<std::atomic<bool>>(false);
    m_items.push_back(std::move(item));
}

std::vector<QueueItemSnapshot> DownloadQueue::items() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<QueueItemSnapshot> out;
    out.reserve(m_items.size());
    for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
        QueueItemSnapshot snap;
        snap.taskId           = it->taskId;
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
        snap.fileSizeBytes    = it->fileSizeBytes;
        snap.speedBytesPerSec = it->speedBytesPerSec;
        snap.etaSeconds       = it->etaSeconds;
        out.push_back(std::move(snap));
    }
    return out;
}

void DownloadQueue::workerLoop() {
    while (!m_stop.load()) {
        long long taskId = 0;
        romm::Config config;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& item : m_items) {
                if (item.state == QueueItemState::Queued) {
                    taskId = item.taskId;
                    item.state = QueueItemState::Downloading;
                    item.bytesReceived = 0;
                    item.speedBytesPerSec = 0;
                    item.etaSeconds = -1;
                    config = m_config;
                    break;
                }
            }
        }

        if (taskId == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            continue;
        }

        romm::RommClient client(config);
        std::string error;
        auto lastTick = std::chrono::steady_clock::now();
        long long lastBytes = 0;
        QueueItemSnapshot current;
        std::shared_ptr<std::atomic<bool>> cancelFlag;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = std::find_if(m_items.begin(), m_items.end(),
                                   [taskId](const QueueItem& item) { return item.taskId == taskId; });
            if (it == m_items.end()) continue;
            current.taskId         = it->taskId;
            current.romId          = it->romId;
            current.title          = it->title;
            current.platformName   = it->platformName;
            current.fileName       = it->fileName;
            current.coverPathSmall = it->coverPathSmall;
            current.fileSizeBytes  = it->fileSizeBytes;
            cancelFlag            = it->cancelFlag;
        }
        if (!cancelFlag) cancelFlag = std::make_shared<std::atomic<bool>>(false);

        romm::Rom rom;
        rom.id             = current.romId;
        rom.name           = current.title;
        rom.platformName   = current.platformName;
        rom.fileName       = current.fileName;
        rom.fileSizeBytes  = current.fileSizeBytes;
        rom.coverPathSmall = current.coverPathSmall;

        const std::string destPath = [&]() {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = std::find_if(m_items.begin(), m_items.end(),
                                   [taskId](const QueueItem& item) { return item.taskId == taskId; });
            return (it == m_items.end()) ? std::string() : it->destPath;
        }();

        bool ok = client.downloadRom(
            rom, destPath,
            [this, taskId, &lastTick, &lastBytes](long long recv, long long total) {
                auto now = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count();
                long long speed = 0;
                if (ms > 0) {
                    speed = (recv - lastBytes) * 1000 / ms;
                }
                lastTick = now;
                lastBytes = recv;

                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = std::find_if(m_items.begin(), m_items.end(),
                                       [taskId](const QueueItem& item) { return item.taskId == taskId; });
                if (it == m_items.end()) return;
                auto& item = *it;
                item.bytesReceived = recv;
                item.bytesTotal = total;
                item.speedBytesPerSec = std::max(0LL, speed);
                if (item.speedBytesPerSec > 0 && total > recv)
                    item.etaSeconds = static_cast<int>((total - recv) / item.speedBytesPerSec);
                else
                    item.etaSeconds = -1;
            },
            *cancelFlag,
            error);

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::find_if(m_items.begin(), m_items.end(),
                               [taskId](const QueueItem& item) { return item.taskId == taskId; });
        if (it == m_items.end()) continue;
        auto& item = *it;
        if (cancelFlag && cancelFlag->load()) {
            item.state = QueueItemState::Cancelled;
            item.bytesReceived = 0;
            item.bytesTotal = item.fileSizeBytes;
            item.error.clear();
        } else if (ok) {
            item.state = QueueItemState::Completed;
            item.bytesReceived = item.bytesTotal;
            item.speedBytesPerSec = 0;
            item.etaSeconds = 0;
            item.error.clear();
        } else {
            item.state = QueueItemState::Failed;
            item.bytesReceived = 0;
            item.speedBytesPerSec = 0;
            item.etaSeconds = -1;
            item.error = error;
        }
    }
}
