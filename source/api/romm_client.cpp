#include "api/romm_client.hpp"

#include <curl/curl.h>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// JSON parsing helpers (lightweight, no external library needed)
// ---------------------------------------------------------------------------

namespace {

// Returns the raw JSON value (string, number, object, or array) for a given
// key in a flat/shallow JSON object.
static std::string jsonGet(const std::string& json, const std::string& key) {
    std::string sk = "\"" + key + "\":";
    auto pos = json.find(sk);
    if (pos == std::string::npos) return {};
    pos += sk.size();
    // Skip whitespace
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size()) return {};

    char first = json[pos];
    if (first == '"') {
        // String value
        ++pos;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                if (json[pos] == '"') val += '"';
                else if (json[pos] == 'n') val += '\n';
                else if (json[pos] == 't') val += '\t';
                else if (json[pos] == '\\') val += '\\';
                else val += json[pos];
            } else {
                val += json[pos];
            }
            ++pos;
        }
        return val;
    }
    // Number / bool / null / object / array — read until delimiter
    size_t end = pos;
    int depth = 0;
    bool inStr = false;
    while (end < json.size()) {
        char c = json[end];
        if (inStr) {
            if (c == '\\') { ++end; }
            else if (c == '"') inStr = false;
        } else {
            if (c == '"') inStr = true;
            else if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') {
                if (depth == 0) break;
                --depth;
            } else if ((c == ',' || c == '\n') && depth == 0) break;
        }
        ++end;
    }
    return json.substr(pos, end - pos);
}

// Parse an integer from a JSON value string
static int jsonInt(const std::string& val) {
    if (val.empty()) return 0;
    try { return std::stoi(val); } catch (...) { return 0; }
}

static long long jsonLL(const std::string& val) {
    if (val.empty()) return 0;
    try { return std::stoll(val); } catch (...) { return 0; }
}

// Split a JSON array string (the "[...]" part) into individual object strings
static std::vector<std::string> jsonSplitArray(const std::string& arr) {
    std::vector<std::string> items;
    if (arr.size() < 2 || arr.front() != '[') return items;

    size_t pos = 1;
    while (pos < arr.size() && arr[pos] != ']') {
        // Skip whitespace / commas
        while (pos < arr.size() && (arr[pos] == ',' || arr[pos] == ' ' ||
               arr[pos] == '\n' || arr[pos] == '\r' || arr[pos] == '\t')) ++pos;
        if (pos >= arr.size() || arr[pos] == ']') break;

        // Find the matching closing brace/bracket/quote
        size_t start = pos;
        int depth = 0;
        bool inStr = false;
        while (pos < arr.size()) {
            char c = arr[pos];
            if (inStr) {
                if (c == '\\') { ++pos; }
                else if (c == '"') inStr = false;
            } else {
                if (c == '"') inStr = true;
                else if (c == '{' || c == '[') ++depth;
                else if (c == '}' || c == ']') {
                    if (depth == 0) break;
                    --depth;
                } else if (c == ',' && depth == 0) break;
            }
            ++pos;
        }
        items.push_back(arr.substr(start, pos - start));
        if (pos < arr.size() && arr[pos] == ',') ++pos;
    }
    return items;
}

// Extract a JSON array from a top-level key
static std::vector<std::string> jsonGetArray(const std::string& json,
                                              const std::string& key) {
    std::string val = jsonGet(json, key);
    return jsonSplitArray(val);
}

// Write callback for libcurl (string buffer)
static size_t writeStringCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = reinterpret_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// Write callback for libcurl (file download)
struct DownloadState {
    FILE* fp;
    romm::ProgressCallback cb;
    const std::atomic<bool>* cancel;
};

static size_t writeFileCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* state = reinterpret_cast<DownloadState*>(userdata);
    return fwrite(ptr, size, nmemb, state->fp);
}

static int progressCb(void* userdata, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* state = reinterpret_cast<DownloadState*>(userdata);
    if (state->cb && dltotal > 0) {
        state->cb(static_cast<long long>(dlnow),
                  static_cast<long long>(dltotal));
    }
    // Return non-zero to abort if cancellation has been requested
    if (state->cancel && state->cancel->load()) return 1;
    return 0;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RomM object parsers
// ---------------------------------------------------------------------------

namespace romm {

static Platform parsePlatform(const std::string& obj) {
    Platform p;
    p.id          = jsonInt(jsonGet(obj, "id"));
    p.name        = jsonGet(obj, "name");
    p.slug        = jsonGet(obj, "slug");
    p.fsSlug      = jsonGet(obj, "fs_slug");
    p.displayName = jsonGet(obj, "display_name");
    p.romCount    = jsonInt(jsonGet(obj, "rom_count"));
    return p;
}

static Collection parseCollection(const std::string& obj) {
    Collection c;
    c.id       = jsonInt(jsonGet(obj, "id"));
    c.name     = jsonGet(obj, "name");
    c.romCount = jsonInt(jsonGet(obj, "rom_count"));
    return c;
}

static Rom parseRom(const std::string& obj) {
    Rom r;
    r.id             = jsonInt(jsonGet(obj, "id"));
    r.name           = jsonGet(obj, "name");
    r.fileName       = jsonGet(obj, "fs_name");
    r.fileSizeBytes  = jsonLL(jsonGet(obj, "fs_size_bytes"));
    r.platformId     = jsonInt(jsonGet(obj, "platform_id"));
    r.platformName   = jsonGet(obj, "platform_display_name");
    r.platformSlug   = jsonGet(obj, "platform_slug");
    r.platformFsSlug = jsonGet(obj, "platform_fs_slug");
    r.summary        = jsonGet(obj, "summary");

    // Parse regions array
    auto regionItems = jsonGetArray(obj, "regions");
    for (auto& ri : regionItems) {
        // Region may be a plain string or object
        if (!ri.empty() && ri.front() == '"') {
            // strip quotes
            r.regions.push_back(ri.substr(1, ri.size() > 2 ? ri.size() - 2 : 0));
        } else {
            std::string rname = jsonGet(ri, "name");
            if (!rname.empty()) r.regions.push_back(rname);
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// RommClient implementation
// ---------------------------------------------------------------------------

RommClient::RommClient(const Config& config)
    : m_config(config)
    , m_curl(nullptr)
    , m_userpwd(config.username + ":" + config.password)
{
    // curl_global_init() must be called once per process by the application
    // before constructing any RommClient instance.
    m_curl = curl_easy_init();
}

RommClient::~RommClient() {
    if (m_curl) curl_easy_cleanup(static_cast<CURL*>(m_curl));
    // curl_global_cleanup() is called by the application, not here.
}

void RommClient::applyBasicAuth() {
    CURL* curl = static_cast<CURL*>(m_curl);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERPWD, m_userpwd.c_str());
}

std::string RommClient::apiUrl(const std::string& path) const {
    std::string base = m_config.serverUrl;
    // Strip trailing slash
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + path;
}

std::string RommClient::httpGet(const std::string& url, std::string& errorOut) {
    CURL* curl = static_cast<CURL*>(m_curl);
    std::string response;

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    // Accept self-signed certificates (typical for home servers)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // HTTP Basic Authentication
    applyBasicAuth();

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        errorOut = curl_easy_strerror(res);
        return {};
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode >= 400) {
        errorOut = "HTTP " + std::to_string(httpCode);
        return {};
    }

    return response;
}

std::string RommClient::httpPost(const std::string& url, const std::string& body,
                                  std::string& errorOut) {
    CURL* curl = static_cast<CURL*>(m_curl);
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // HTTP Basic Authentication
    applyBasicAuth();

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        errorOut = curl_easy_strerror(res);
        return {};
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode >= 400) {
        errorOut = "HTTP " + std::to_string(httpCode) + ": " + response;
        return {};
    }

    return response;
}

bool RommClient::login(std::string& errorOut) {
    // With HTTP Basic Auth, credentials are sent on every request.
    // Validate them by making a GET to /api/platforms — a lightweight,
    // authenticated endpoint that returns quickly and is always available.
    CURL* curl = static_cast<CURL*>(m_curl);
    std::string response;

    std::string url = apiUrl("/api/platforms");

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // HTTP Basic Authentication
    applyBasicAuth();

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        errorOut = curl_easy_strerror(res);
        return false;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode == 401 || httpCode == 403) {
        errorOut = "Authentication failed (HTTP " + std::to_string(httpCode) + ")";
        return false;
    }
    if (httpCode >= 400) {
        errorOut = "Login failed (HTTP " + std::to_string(httpCode) + ")";
        return false;
    }

    return true;
}

std::vector<Platform> RommClient::getPlatforms(std::string& errorOut) {
    std::string body = httpGet(apiUrl("/api/platforms"), errorOut);
    if (body.empty()) return {};

    std::vector<Platform> result;
    for (auto& item : jsonSplitArray(body)) {
        if (!item.empty()) result.push_back(parsePlatform(item));
    }
    return result;
}

std::vector<Rom> RommClient::getRoms(int platformId, std::string& errorOut) {
    std::string url = apiUrl("/api/roms?platform_id=" +
                             std::to_string(platformId) +
                             "&limit=" + std::to_string(ROM_PAGE_SIZE));
    std::string body = httpGet(url, errorOut);
    if (body.empty()) return {};

    // Response may be {"items":[...],"total":N,...}
    std::string itemsJson = jsonGet(body, "items");
    std::vector<std::string> items;
    if (!itemsJson.empty() && itemsJson.front() == '[') {
        items = jsonSplitArray(itemsJson);
    } else {
        // Fallback: bare array
        items = jsonSplitArray(body);
    }

    std::vector<Rom> result;
    for (auto& item : items) {
        if (!item.empty()) result.push_back(parseRom(item));
    }
    return result;
}

std::vector<Rom> RommClient::getRomsByCollection(int collectionId,
                                                   std::string& errorOut) {
    std::string url = apiUrl("/api/collections/" + std::to_string(collectionId) +
                             "/roms?limit=" + std::to_string(ROM_PAGE_SIZE));
    std::string body = httpGet(url, errorOut);
    if (body.empty()) return {};

    std::string itemsJson = jsonGet(body, "items");
    std::vector<std::string> items;
    if (!itemsJson.empty() && itemsJson.front() == '[') {
        items = jsonSplitArray(itemsJson);
    } else {
        items = jsonSplitArray(body);
    }

    std::vector<Rom> result;
    for (auto& item : items) {
        if (!item.empty()) result.push_back(parseRom(item));
    }
    return result;
}

std::vector<Collection> RommClient::getCollections(std::string& errorOut) {
    std::string body = httpGet(apiUrl("/api/collections"), errorOut);
    if (body.empty()) return {};

    std::vector<Collection> result;
    for (auto& item : jsonSplitArray(body)) {
        if (!item.empty()) result.push_back(parseCollection(item));
    }
    return result;
}

Rom RommClient::getRom(int romId, std::string& errorOut) {
    std::string body = httpGet(apiUrl("/api/roms/" + std::to_string(romId)),
                               errorOut);
    if (body.empty()) return {};
    return parseRom(body);
}

bool RommClient::downloadRom(const Rom& rom, const std::string& destPath,
                              ProgressCallback progressCb,
                              const std::atomic<bool>& cancelFlag,
                              std::string& errorOut) {
    // Ensure destination directory exists by creating all parent dirs
    {
        std::string dir = destPath;
        auto sep = dir.rfind('/');
        if (sep != std::string::npos) {
            dir = dir.substr(0, sep);
            // Create directories recursively
            for (size_t i = 1; i < dir.size(); ++i) {
                if (dir[i] == '/') {
                    std::string sub = dir.substr(0, i);
                    mkdir(sub.c_str(), 0755);
                }
            }
            mkdir(dir.c_str(), 0755);
        }
    }

    FILE* fp = fopen(destPath.c_str(), "wb");
    if (!fp) {
        errorOut = "Cannot open destination file: " + destPath;
        return false;
    }

    std::string url = apiUrl("/api/roms/" + std::to_string(rom.id) +
                             "/content/" + rom.fileName);

    DownloadState state{fp, progressCb, &cancelFlag};
    CURL* curl = static_cast<CURL*>(m_curl);

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFileCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    // Always set XFERINFO so we can check the cancel flag periodically
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ::progressCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); // No timeout for large files
    // SSL peer verification is disabled to support self-signed certificates
    // commonly used on home servers. The user is connecting to a server they
    // configured themselves, so MITM risk is low in this context.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // HTTP Basic Authentication
    applyBasicAuth();

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);

    if (cancelFlag.load()) {
        remove(destPath.c_str()); // Clean up partial file on cancellation
        errorOut = "Download cancelled.";
        return false;
    }

    if (res != CURLE_OK) {
        remove(destPath.c_str()); // Clean up partial file on error
        errorOut = curl_easy_strerror(res);
        return false;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode >= 400) {
        remove(destPath.c_str());
        errorOut = "HTTP " + std::to_string(httpCode);
        return false;
    }

    return true;
}

} // namespace romm
