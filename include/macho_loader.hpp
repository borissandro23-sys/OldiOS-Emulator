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

struct MachODylib {
    std::string name;
    std::uint32_t command = 0;
    std::uint32_t currentVersion = 0;
    std::uint32_t compatibilityVersion = 0;
};

struct MachOSymbolTable {
    bool present = false;
    std::uint32_t symbolOffset = 0;
    std::uint32_t symbolCount = 0;
    std::uint32_t stringOffset = 0;
    std::uint32_t stringSize = 0;
};

struct MachOIndirectSymbols {
    bool present = false;
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

struct MachOImage {
    bool bigEndian = false;
    std::uint32_t cpuType = 0, cpuSubtype = 0, fileType = 0, entryPoint = 0;
    bool hasEntryPoint = false;
    std::string installName;
    std::vector<MachOSegment> segments;
    std::vector<MachODylib> dylibs;
    MachOSymbolTable symbolTable;
    MachOIndirectSymbols indirectSymbols;
    std::uint64_t totalMappedBytes = 0;
};

// Parses a thin 32-bit ARM Mach-O, materializes non-PAGEZERO segments,
// and records dynamic-library and symbol-table metadata. It does not load
// dependent libraries, perform relocations, or execute code.
bool loadMachOImage(const std::filesystem::path& path, MachOImage& image, std::string& error);
