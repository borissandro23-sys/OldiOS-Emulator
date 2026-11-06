#pragma once

#include <filesystem>
#include <string>

struct IpaInfo {
    bool valid = false;
    std::string appBundlePath;
    std::string infoPlistPath;
    unsigned int fileCount = 0;
    std::string error;
};

// Checks the ZIP directory and locates Payload/<name>.app/Info.plist.
// It does not extract or execute the application.
bool inspectIpa(const std::filesystem::path& filePath, IpaInfo& result);
