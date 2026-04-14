#include "config.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// Simple JSON helpers — avoids a heavy dependency just for config
// Format: {"key":"value",...}

namespace romm {

static const char* CONFIG_PATH = "sdmc:/config/romm2switch/config.json";
static const char* CONFIG_DIR  = "sdmc:/config/romm2switch";

// ---------------------------------------------------------------------------
// Minimal JSON read helpers
// ---------------------------------------------------------------------------

static std::string jsonGetString(const std::string& json, const std::string& key) {
    // Find "key":"
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos += search.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    std::string val = json.substr(pos, end - pos);
    // Unescape \" sequences
    std::string result;
    for (size_t i = 0; i < val.size(); ++i) {
        if (val[i] == '\\' && i + 1 < val.size() && val[i + 1] == '"') {
            result += '"';
            ++i;
        } else {
            result += val[i];
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Minimal JSON write helper
// ---------------------------------------------------------------------------

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else { out += c; }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Config loadConfig() {
    Config cfg;

    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f) return cfg;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size <= 0 || size > 4096) { fclose(f); return cfg; }

    std::string buf(static_cast<size_t>(size), '\0');
    fread(&buf[0], 1, static_cast<size_t>(size), f);
    fclose(f);

    cfg.serverUrl    = jsonGetString(buf, "server_url");
    cfg.username     = jsonGetString(buf, "username");
    cfg.password     = jsonGetString(buf, "password");
    std::string dp   = jsonGetString(buf, "download_path");
    if (!dp.empty()) cfg.downloadPath = dp;

    return cfg;
}

bool saveConfig(const Config& cfg) {
    // Ensure directory exists
    mkdir(CONFIG_DIR, 0755);

    FILE* f = fopen(CONFIG_PATH, "w");
    if (!f) return false;

    fprintf(f,
        "{"
        "\"server_url\":\"%s\","
        "\"username\":\"%s\","
        "\"password\":\"%s\","
        "\"download_path\":\"%s\""
        "}\n",
        jsonEscape(cfg.serverUrl).c_str(),
        jsonEscape(cfg.username).c_str(),
        jsonEscape(cfg.password).c_str(),
        jsonEscape(cfg.downloadPath).c_str());

    fclose(f);
    return true;
}

} // namespace romm
