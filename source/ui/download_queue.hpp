#pragma once

#include "config.hpp"
#include "models/models.hpp"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class QueueItemState { Queued, Downloading, Completed, Failed, Cancelled };

struct QueueItemSnapshot {
    long long   taskId = 0;
    int         romId = 0;
    std::string title;
    std::string platformName;
    std::string fileName;
    std::string coverPathSmall;
    std::string destPath;
    std::string error;
    QueueItemState state = QueueItemState::Queued;
    long long   bytesReceived = 0;
    long long   bytesTotal = 0;
    long long   fileSizeBytes = 0;
    long long   speedBytesPerSec = 0;
    int         etaSeconds = -1;
};

class DownloadQueue {
public:
    explicit DownloadQueue(const romm::Config& config);
    ~DownloadQueue();

    void updateConfig(const romm::Config& config);
    void enqueue(const romm::Rom& rom, const std::string& destPath);
    std::vector<QueueItemSnapshot> items() const;

private:
    struct QueueItem {
        long long   taskId = 0;
        int         romId = 0;
        std::string title;
        std::string platformName;
        std::string fileName;
        std::string coverPathSmall;
        std::string destPath;
        std::string error;
        QueueItemState state = QueueItemState::Queued;
        long long   bytesReceived = 0;
        long long   bytesTotal = 0;
        long long   fileSizeBytes = 0;
        long long   speedBytesPerSec = 0;
        int         etaSeconds = -1;
        std::shared_ptr<std::atomic<bool>> cancelFlag;
    };

    mutable std::mutex       m_mutex;
    romm::Config             m_config;
    std::deque<QueueItem>    m_items;
    std::thread              m_worker;
    std::atomic<bool>        m_stop{false};
    long long                m_nextTaskId = 1;

    void workerLoop();
};
