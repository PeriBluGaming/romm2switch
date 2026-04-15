#pragma once

#include <string>
#include <vector>

namespace romm {

struct Platform {
    int id = 0;
    std::string name;
    std::string slug;
    std::string fsSlug;
    std::string displayName;
    int romCount = 0;
};

struct Collection {
    int id = 0;
    std::string name;
    int romCount = 0;
};

struct Rom {
    int id = 0;
    std::string name;
    std::string fileName;       // parsed from "fs_name" in API 4.7.0
    long long fileSizeBytes = 0; // parsed from "fs_size_bytes" in API 4.7.0
    int platformId = 0;
    std::string platformName;   // parsed from "platform_display_name" in API 4.7.0
    std::string platformSlug;
    std::string platformFsSlug;
    std::string summary;
    std::vector<std::string> regions;

    // Returns human-readable file size string
    std::string fileSizeStr() const {
        if (fileSizeBytes < 1024)
            return std::to_string(fileSizeBytes) + " B";
        if (fileSizeBytes < 1024 * 1024)
            return std::to_string(fileSizeBytes / 1024) + " KB";
        if (fileSizeBytes < 1024LL * 1024 * 1024)
            return std::to_string(fileSizeBytes / (1024 * 1024)) + " MB";
        return std::to_string(fileSizeBytes / (1024LL * 1024 * 1024)) + " GB";
    }
};

} // namespace romm
