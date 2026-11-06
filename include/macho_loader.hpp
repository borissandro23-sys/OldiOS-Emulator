#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct MachOSegment {
    std::string name;
    std::uint32_t vmAddress = 0, vmSize = 0, fileOffset = 0, fileSize = 0;
    std::vector<std::uint8_t> memory;
};

struct MachOSection {
    std::string segmentName, sectionName;
    std::uint32_t address = 0, size = 0, fileOffset = 0, flags = 0;
    std::uint32_t reserved1 = 0, reserved2 = 0;
};

struct MachOSymbol {
    std::string name;
    std::uint8_t type = 0, section = 0;
    std::uint16_t description = 0;
    std::uint32_t value = 0;
};

struct MachOSymbolTable {
    bool present = false;
    std::uint32_t symbolOffset = 0, symbolCount = 0;
    std::uint32_t stringOffset = 0, stringSize = 0;
};

struct MachODynamicSymbolTable {
    bool present = false;
    std::uint32_t undefinedSymbolIndex = 0, undefinedSymbolCount = 0;
    std::uint32_t indirectSymbolOffset = 0, indirectSymbolCount = 0;
    std::vector<std::uint32_t> indirectSymbols;
};

struct MachOImage {
    bool bigEndian = false;
    std::uint32_t cpuType = 0, cpuSubtype = 0, fileType = 0, entryPoint = 0;
    bool hasEntryPoint = false;
    bool hasInitialThreadState = false;
    std::array<std::uint32_t, 16> initialRegisters{};
    std::uint32_t initialCpsr = 0;
    std::vector<MachOSegment> segments;
    std::vector<std::string> dependencies;
    bool hasDyldInfo = false;
    bool hasDylinker = false;
    std::string dylinkerPath;
    MachOSymbolTable symbolTable;
    MachODynamicSymbolTable dynamicSymbolTable;
    std::vector<MachOSection> sections;
    std::vector<MachOSymbol> symbols;
    std::uint64_t totalMappedBytes = 0;
};

// Materializes non-PAGEZERO segments and reports linked dylib names from a thin 32-bit ARM Mach-O.
// This does not load dependent images, apply dyld fixups, execute code, or emulate iOS APIs.
bool loadMachOImage(const std::filesystem::path& path, MachOImage& image, std::string& error);
