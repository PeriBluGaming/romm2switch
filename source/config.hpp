#pragma once

#include <string>

namespace romm {

struct Config {
    std::string serverUrl;
    std::string username;
    std::string password;
    std::string downloadPath = "sdmc:/roms/";

    bool isConfigured() const {
        return !serverUrl.empty() && !username.empty();
    }
};

// Loads config from sdmc:/config/romm2switch/config.json.
// Returns default Config if file does not exist.
Config loadConfig();

// Saves config to sdmc:/config/romm2switch/config.json.
// Returns true on success.
bool saveConfig(const Config& config);

} // namespace romm
