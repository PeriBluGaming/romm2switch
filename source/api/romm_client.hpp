#pragma once

#include "models/models.hpp"
#include "config.hpp"

#include <functional>
#include <string>
#include <vector>

namespace romm {

// Progress callback: (bytesDownloaded, totalBytes)
using ProgressCallback = std::function<void(long long, long long)>;

class RommClient {
public:
    explicit RommClient(const Config& config);
    ~RommClient();

    // --- Authentication ---
    // Authenticates with the server and stores the session cookie.
    // Returns true on success, false otherwise.
    bool login(std::string& errorOut);

    // --- Browse ---
    std::vector<Platform>   getPlatforms(std::string& errorOut);
    std::vector<Rom>        getRoms(int platformId, std::string& errorOut);
    std::vector<Rom>        getRomsByCollection(int collectionId, std::string& errorOut);
    std::vector<Collection> getCollections(std::string& errorOut);
    Rom                     getRom(int romId, std::string& errorOut);

    // --- Download ---
    // Downloads a ROM to destPath (full path including filename).
    // progressCb is called periodically with (bytesReceived, totalBytes).
    // Returns true on success.
    bool downloadRom(const Rom& rom, const std::string& destPath,
                     ProgressCallback progressCb, std::string& errorOut);

private:
    Config m_config;
    void*  m_curl;        // CURL* handle (opaque to keep curl out of header)
    std::string m_cookieJarPath;

    // Perform a GET request; returns HTTP body or empty string on error.
    std::string httpGet(const std::string& url, std::string& errorOut);

    // Perform a POST request with JSON body; returns HTTP body or empty on error.
    std::string httpPost(const std::string& url, const std::string& body,
                         std::string& errorOut);

    // Build full API URL from a relative path.
    std::string apiUrl(const std::string& path) const;
};

} // namespace romm
