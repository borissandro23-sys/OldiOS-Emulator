#include "macho_loader.hpp"
#include <algorithm>
#include <fstream>

namespace {
constexpr std::uint32_t MH_MAGIC = 0xfeedfaceu, MH_CIGAM = 0xcefaedfeu;
constexpr std::uint32_t LC_SEGMENT = 0x1u, LC_SYMTAB = 0x2u;
constexpr std::uint32_t LC_UNIXTHREAD = 0x5u, LC_DYSYMTAB = 0xBu;
constexpr std::uint32_t LC_LOAD_DYLIB = 0xCu, LC_ID_DYLIB = 0xDu;
constexpr std::uint32_t LC_LOAD_WEAK_DYLIB = 0x80000018u;
constexpr std::uint32_t LC_REEXPORT_DYLIB = 0x8000001Fu;
constexpr std::uint32_t LC_LAZY_LOAD_DYLIB = 0x20u;
constexpr std::uint32_t LC_LOAD_UPWARD_DYLIB = 0x80000023u;
constexpr std::uint64_t MAX_FILE_SIZE = 256ull * 1024 * 1024;
constexpr std::uint64_t MAX_MAPPED_SIZE = 256ull * 1024 * 1024;

std::uint32_t read32(const std::vector<std::uint8_t>& b, std::size_t p, bool be) {
    if (p > b.size() || b.size() - p < 4) return 0;
    if (be) return (std::uint32_t(b[p]) << 24) | (std::uint32_t(b[p+1]) << 16) |
                   (std::uint32_t(b[p+2]) << 8) | std::uint32_t(b[p+3]);
    return std::uint32_t(b[p]) | (std::uint32_t(b[p+1]) << 8) |
           (std::uint32_t(b[p+2]) << 16) | (std::uint32_t(b[p+3]) << 24);
}

bool rangeFits(std::uint32_t offset, std::uint32_t size, std::size_t total) {
    return std::uint64_t(offset) + size <= total;
}

bool readCommandString(const std::vector<std::uint8_t>& bytes, std::size_t command,
                       std::uint32_t commandSize, std::uint32_t stringOffset,
                       std::string& result) {
    if (stringOffset < 8 || stringOffset >= commandSize) return false;
    const std::size_t begin = command + stringOffset;
    const std::size_t end = command + commandSize;
    std::size_t cursor = begin;
    while (cursor < end && bytes[cursor] != 0) ++cursor;
    if (cursor == end) return false;
    result.assign(reinterpret_cast<const char*>(bytes.data() + begin), cursor - begin);
    return true;
}

bool isDylibLoadCommand(std::uint32_t command) {
    return command == LC_LOAD_DYLIB || command == LC_LOAD_WEAK_DYLIB ||
           command == LC_REEXPORT_DYLIB || command == LC_LAZY_LOAD_DYLIB ||
           command == LC_LOAD_UPWARD_DYLIB;
}
}

bool loadMachOImage(const std::filesystem::path& path, MachOImage& image, std::string& error) {
    image = {};
    error.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) { error = "Cannot open the extracted Mach-O executable."; return false; }
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 28 || static_cast<std::uint64_t>(end) > MAX_FILE_SIZE) {
        error = "Mach-O file size is invalid or exceeds 256 MiB."; return false;
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "Could not read the complete Mach-O file."; return false;
    }

    const auto magic = read32(bytes, 0, false);
    if (magic == MH_MAGIC) image.bigEndian = false;
    else if (magic == MH_CIGAM) image.bigEndian = true;
    else { error = "Only thin 32-bit Mach-O files are supported."; return false; }

    const bool be = image.bigEndian;
    image.cpuType = read32(bytes, 4, be);
    image.cpuSubtype = read32(bytes, 8, be);
    image.fileType = read32(bytes, 12, be);
    const auto ncmds = read32(bytes, 16, be);
    const auto sizeofcmds = read32(bytes, 20, be);
    if (image.cpuType != 12u || image.fileType != 2u) {
        error = "Expected a 32-bit ARM MH_EXECUTE image."; return false;
    }
    if (ncmds > 4096u || std::uint64_t(28) + sizeofcmds > bytes.size()) {
        error = "Mach-O load-command table is invalid."; return false;
    }

    std::size_t pos = 28;
    const std::size_t commandEnd = 28u + sizeofcmds;
    for (std::uint32_t i = 0; i < ncmds; ++i) {
        if (pos > commandEnd || commandEnd - pos < 8) {
            error = "A Mach-O load command is truncated."; return false;
        }
        const auto cmd = read32(bytes, pos, be);
        const auto cmdsize = read32(bytes, pos + 4, be);
        if (cmdsize < 8 || cmdsize > commandEnd - pos) {
            error = "A Mach-O load command has an invalid size."; return false;
        }

        if (cmd == LC_SEGMENT) {
            if (cmdsize < 56) { error = "LC_SEGMENT command is too small."; return false; }
            std::string name;
            for (std::size_t n = 0; n < 16 && bytes[pos + 8 + n]; ++n)
                name.push_back(static_cast<char>(bytes[pos + 8 + n]));
            const auto vmaddr = read32(bytes, pos + 24, be);
            const auto vmsize = read32(bytes, pos + 28, be);
            const auto fileoff = read32(bytes, pos + 32, be);
            const auto filesize = read32(bytes, pos + 36, be);
            if (filesize > vmsize || !rangeFits(fileoff, filesize, bytes.size())) {
                error = "A segment has invalid file or virtual-memory bounds."; return false;
            }
            if (name != "__PAGEZERO" && vmsize) {
                if (image.totalMappedBytes + vmsize > MAX_MAPPED_SIZE) {
                    error = "Segments exceed the 256 MiB mapping limit."; return false;
                }
                MachOSegment seg;
                seg.name = name; seg.vmAddress = vmaddr; seg.vmSize = vmsize;
                seg.fileOffset = fileoff; seg.fileSize = filesize;
                seg.memory.assign(vmsize, 0);
                if (filesize) std::copy_n(bytes.begin() + fileoff, filesize, seg.memory.begin());
                image.totalMappedBytes += vmsize;
                image.segments.push_back(std::move(seg));
            }
        } else if (cmd == LC_ID_DYLIB || isDylibLoadCommand(cmd)) {
            if (cmdsize < 24) { error = "A dylib load command is too small."; return false; }
            MachODylib dylib;
            dylib.command = cmd;
            const auto nameOffset = read32(bytes, pos + 8, be);
            dylib.currentVersion = read32(bytes, pos + 16, be);
            dylib.compatibilityVersion = read32(bytes, pos + 20, be);
            if (!readCommandString(bytes, pos, cmdsize, nameOffset, dylib.name)) {
                error = "A dylib command contains an invalid or unterminated name."; return false;
            }
            if (cmd == LC_ID_DYLIB) {
                if (!image.installName.empty()) {
                    error = "Mach-O contains more than one LC_ID_DYLIB command."; return false;
                }
                image.installName = dylib.name;
            } else {
                image.dylibs.push_back(std::move(dylib));
            }
        } else if (cmd == LC_SYMTAB) {
            if (cmdsize < 24 || image.symbolTable.present) {
                error = "LC_SYMTAB command is malformed or duplicated."; return false;
            }
            MachOSymbolTable table;
            table.present = true;
            table.symbolOffset = read32(bytes, pos + 8, be);
            table.symbolCount = read32(bytes, pos + 12, be);
            table.stringOffset = read32(bytes, pos + 16, be);
            table.stringSize = read32(bytes, pos + 20, be);
            const std::uint64_t symbolBytes = std::uint64_t(table.symbolCount) * 12u;
            if (symbolBytes > UINT32_MAX ||
                !rangeFits(table.symbolOffset, static_cast<std::uint32_t>(symbolBytes), bytes.size()) ||
                !rangeFits(table.stringOffset, table.stringSize, bytes.size())) {
                error = "Mach-O symbol or string table extends beyond the file."; return false;
            }
            image.symbolTable = table;
        } else if (cmd == LC_DYSYMTAB) {
            if (cmdsize < 80 || image.indirectSymbols.present) {
                error = "LC_DYSYMTAB command is malformed or duplicated."; return false;
            }
            MachOIndirectSymbols table;
            table.present = true;
            table.offset = read32(bytes, pos + 56, be);
            table.count = read32(bytes, pos + 60, be);
            const std::uint64_t tableBytes = std::uint64_t(table.count) * 4u;
            if (tableBytes > UINT32_MAX ||
                !rangeFits(table.offset, static_cast<std::uint32_t>(tableBytes), bytes.size())) {
                error = "Mach-O indirect-symbol table extends beyond the file."; return false;
            }
            image.indirectSymbols = table;
        } else if (cmd == LC_UNIXTHREAD && cmdsize >= 16) {
            std::size_t state = pos + 8;
            const std::size_t cmdEnd = pos + cmdsize;
            while (state + 8 <= cmdEnd) {
                const auto flavor = read32(bytes, state, be);
                const auto count = read32(bytes, state + 4, be);
                state += 8;
                const std::uint64_t stateBytes = std::uint64_t(count) * 4;
                if (stateBytes > cmdEnd - state) break;
                if (flavor == 1u && count >= 16u) {
                    image.entryPoint = read32(bytes, state + 15u * 4u, be);
                    image.hasEntryPoint = true;
                    break;
                }
                state += static_cast<std::size_t>(stateBytes);
            }
        }
        pos += cmdsize;
    }
    if (pos != commandEnd || image.segments.empty()) {
        error = image.segments.empty() ? "No loadable segments were found."
                                       : "Load-command sizes do not match sizeofcmds.";
        image = {}; return false;
    }
    if (image.hasEntryPoint) {
        const bool valid = std::any_of(image.segments.begin(), image.segments.end(),
            [&](const MachOSegment& s) {
                return image.entryPoint >= s.vmAddress &&
                    std::uint64_t(image.entryPoint) < std::uint64_t(s.vmAddress) + s.vmSize;
            });
        if (!valid) { image.hasEntryPoint = false; image.entryPoint = 0; }
    }
    return true;
}
