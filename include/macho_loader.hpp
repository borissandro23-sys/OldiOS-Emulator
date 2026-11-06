#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct MachOSegment {
    std::string name;
    std::uint32_t vmAddress = 0, vmSize = 0, fileOffset = 0, fileSize = 0;
    std::vector<std::uint8_t> memory;
};

struct MachOImage {
    bool bigEndian = false;
    std::uint32_t cpuType = 0, cpuSubtype = 0, fileType = 0, entryPoint = 0;
    bool hasEntryPoint = false;
    std::vector<MachOSegment> segments;
    std::uint64_t totalMappedBytes = 0;
};

// Materializes non-PAGEZERO segments from a thin 32-bit ARM Mach-O.
// This parses and maps bytes; it does not execute code or emulate iOS APIs.
bool loadMachOImage(const std::filesystem::path& path, MachOImage& image, std::string& error);
