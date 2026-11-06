#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct IpaInfo {
    bool valid = false;
    std::string appBundlePath;
    std::string infoPlistPath;
    std::filesystem::path executablePath;
    std::string executableName;
    std::uint32_t cpuType = 0;
    std::uint32_t cpuSubtype = 0;
    std::uint32_t fileType = 0;
    std::uint64_t executableSize = 0;
    std::string error;
};

// Extracts the app's main executable to a safe temporary directory and
// validates its 32-bit Mach-O header. It does not execute the application.
bool loadIpaExecutable(const std::filesystem::path& filePath, IpaInfo& result);
