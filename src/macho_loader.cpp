#include "macho_loader.hpp"
#include <algorithm>
#include <fstream>

namespace {
constexpr std::uint32_t MH_MAGIC = 0xfeedfaceu, MH_CIGAM = 0xcefaedfeu;
constexpr std::uint32_t LC_SEGMENT = 0x1u, LC_SYMTAB = 0x2u;
constexpr std::uint32_t LC_UNIXTHREAD = 0x5u, LC_DYSYMTAB = 0xBu;
constexpr std::uint32_t LC_LOAD_DYLINKER = 0xEu;
constexpr std::uint32_t S_LAZY_SYMBOL_POINTERS = 0x7u;
constexpr std::uint32_t S_NON_LAZY_SYMBOL_POINTERS = 0x6u;
constexpr std::uint32_t S_SYMBOL_STUBS = 0x8u;
constexpr std::uint32_t LC_LOAD_DYLIB = 0xCu;
constexpr std::uint32_t LC_LOAD_WEAK_DYLIB = 0x80000018u;
constexpr std::uint32_t LC_REEXPORT_DYLIB = 0x8000001Fu;
constexpr std::uint32_t LC_LOAD_UPWARD_DYLIB = 0x80000023u;
constexpr std::uint32_t LC_LAZY_LOAD_DYLIB = 0x20u;
constexpr std::uint32_t LC_DYLD_INFO = 0x22u;
constexpr std::uint32_t LC_DYLD_INFO_ONLY = 0x80000022u;
constexpr std::uint64_t MAX_FILE_SIZE = 256ull * 1024 * 1024;
constexpr std::uint64_t MAX_MAPPED_SIZE = 256ull * 1024 * 1024;

std::uint32_t read32(const std::vector<std::uint8_t>& b, std::size_t p, bool be) {
    if (p > b.size() || b.size() - p < 4) return 0;
    if (be) return (std::uint32_t(b[p]) << 24) | (std::uint32_t(b[p+1]) << 16) |
                   (std::uint32_t(b[p+2]) << 8) | std::uint32_t(b[p+3]);
    return std::uint32_t(b[p]) | (std::uint32_t(b[p+1]) << 8) |
           (std::uint32_t(b[p+2]) << 16) | (std::uint32_t(b[p+3]) << 24);
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

        if (cmd == LC_LOAD_DYLINKER) {
            if (cmdsize < 12) {
                error = "LC_LOAD_DYLINKER command is too small.";
                return false;
            }
            const std::uint32_t pathOffset = read32(bytes, pos + 8, be);
            if (pathOffset < 12 || pathOffset >= cmdsize) {
                error = "LC_LOAD_DYLINKER has an invalid path offset.";
                return false;
            }
            const std::size_t pathStart = pos + pathOffset;
            const std::size_t pathLimit = pos + cmdsize;
            std::size_t pathEnd = pathStart;
            while (pathEnd < pathLimit && bytes[pathEnd] != 0) ++pathEnd;
            if (pathEnd == pathLimit) {
                error = "LC_LOAD_DYLINKER path is unterminated.";
                return false;
            }
            image.hasDylinker = true;
            image.dylinkerPath.assign(
                reinterpret_cast<const char*>(bytes.data() + pathStart),
                pathEnd - pathStart);
        } else if (cmd == LC_SYMTAB) {
            if (cmdsize < 24) {
                error = "LC_SYMTAB command is too small.";
                return false;
            }
            image.symbolTable.present = true;
            image.symbolTable.symbolOffset = read32(bytes, pos + 8, be);
            image.symbolTable.symbolCount = read32(bytes, pos + 12, be);
            image.symbolTable.stringOffset = read32(bytes, pos + 16, be);
            image.symbolTable.stringSize = read32(bytes, pos + 20, be);
            const std::uint64_t symbolBytes =
                std::uint64_t(image.symbolTable.symbolCount) * 12u;
            if (std::uint64_t(image.symbolTable.symbolOffset) + symbolBytes > bytes.size() ||
                std::uint64_t(image.symbolTable.stringOffset) +
                    image.symbolTable.stringSize > bytes.size()) {
                error = "LC_SYMTAB points outside the Mach-O file.";
                return false;
            }
        } else if (cmd == LC_DYSYMTAB) {
            if (cmdsize < 80) {
                error = "LC_DYSYMTAB command is too small.";
                return false;
            }
            image.dynamicSymbolTable.present = true;
            image.dynamicSymbolTable.undefinedSymbolIndex = read32(bytes, pos + 24, be);
            image.dynamicSymbolTable.undefinedSymbolCount = read32(bytes, pos + 28, be);
            image.dynamicSymbolTable.indirectSymbolOffset = read32(bytes, pos + 56, be);
            image.dynamicSymbolTable.indirectSymbolCount = read32(bytes, pos + 60, be);
            const auto& ds = image.dynamicSymbolTable;
            if (std::uint64_t(ds.indirectSymbolOffset) +
                    std::uint64_t(ds.indirectSymbolCount) * 4u > bytes.size()) {
                error = "LC_DYSYMTAB indirect-symbol table is outside the Mach-O file.";
                return false;
            }
        }

        if (cmd == LC_LOAD_DYLIB || cmd == LC_LOAD_WEAK_DYLIB ||
            cmd == LC_REEXPORT_DYLIB || cmd == LC_LOAD_UPWARD_DYLIB ||
            cmd == LC_LAZY_LOAD_DYLIB) {
            if (cmdsize < 24) {
                error = "A dylib load command is too small.";
                return false;
            }
            const std::uint32_t nameOffset = read32(bytes, pos + 8, be);
            if (nameOffset < 24 || nameOffset >= cmdsize) {
                error = "A dylib load command has an invalid name offset.";
                return false;
            }
            const std::size_t nameStart = pos + nameOffset;
            const std::size_t nameLimit = pos + cmdsize;
            std::size_t nameEnd = nameStart;
            while (nameEnd < nameLimit && bytes[nameEnd] != 0) ++nameEnd;
            if (nameEnd == nameStart || nameEnd == nameLimit) {
                error = "A dylib load command has an empty or unterminated install name.";
                return false;
            }
            image.dependencies.emplace_back(
                reinterpret_cast<const char*>(bytes.data() + nameStart),
                nameEnd - nameStart);
        } else if (cmd == LC_DYLD_INFO || cmd == LC_DYLD_INFO_ONLY) {
            image.hasDyldInfo = true;
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
            const auto nsects = read32(bytes, pos + 48, be);
            if (std::uint64_t(56) + std::uint64_t(nsects) * 68u > cmdsize) {
                error = "LC_SEGMENT section table exceeds its command.";
                return false;
            }
            if (filesize > vmsize || std::uint64_t(fileoff) + filesize > bytes.size()) {
                error = "A segment has invalid file or virtual-memory bounds."; return false;
            }
            for (std::uint32_t sectionIndex = 0; sectionIndex < nsects; ++sectionIndex) {
                const std::size_t sectionPos = pos + 56u + std::size_t(sectionIndex) * 68u;
                MachOSection section;
                for (std::size_t n = 0; n < 16 && bytes[sectionPos + n]; ++n)
                    section.sectionName.push_back(static_cast<char>(bytes[sectionPos + n]));
                for (std::size_t n = 0; n < 16 && bytes[sectionPos + 16u + n]; ++n)
                    section.segmentName.push_back(static_cast<char>(bytes[sectionPos + 16u + n]));
                section.address = read32(bytes, sectionPos + 32u, be);
                section.size = read32(bytes, sectionPos + 36u, be);
                section.fileOffset = read32(bytes, sectionPos + 40u, be);
                section.flags = read32(bytes, sectionPos + 56u, be);
                section.reserved1 = read32(bytes, sectionPos + 60u, be);
                section.reserved2 = read32(bytes, sectionPos + 64u, be);
                const auto sectionType = section.flags & 0xFFu;
                if (sectionType == S_SYMBOL_STUBS && section.reserved2 == 0) {
                    error = "A symbol-stub section has a zero stub size.";
                    return false;
                }
                if (sectionType != 0x1u && sectionType != 0xC && sectionType != 0x12u &&
                    section.size && std::uint64_t(section.fileOffset) + section.size > bytes.size()) {
                    error = "A file-backed section points outside the Mach-O file.";
                    return false;
                }
                image.sections.push_back(std::move(section));
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
        } else if (cmd == LC_UNIXTHREAD && cmdsize >= 16) {
            std::size_t state = pos + 8;
            const std::size_t cmdEnd = pos + cmdsize;
            while (state + 8 <= cmdEnd) {
                const auto flavor = read32(bytes, state, be);
                const auto count = read32(bytes, state + 4, be);
                state += 8;
                const std::uint64_t stateBytes = std::uint64_t(count) * 4;
                if (stateBytes > cmdEnd - state) break;
                // ARM_THREAD_STATE: r0-r15, CPSR; PC is register r15.
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
    if (image.symbolTable.present) {
        const auto& st = image.symbolTable;
        image.symbols.reserve(st.symbolCount);
        for (std::uint32_t i = 0; i < st.symbolCount; ++i) {
            const std::size_t symbolPos = std::size_t(st.symbolOffset) + std::size_t(i) * 12u;
            const auto stringIndex = read32(bytes, symbolPos, be);
            MachOSymbol symbol;
            symbol.type = bytes[symbolPos + 4u];
            symbol.section = bytes[symbolPos + 5u];
            symbol.description = static_cast<std::uint16_t>(
                std::uint16_t(bytes[symbolPos + 6u]) |
                (std::uint16_t(bytes[symbolPos + 7u]) << 8));
            symbol.value = read32(bytes, symbolPos + 8u, be);
            if (stringIndex < st.stringSize) {
                const std::size_t stringStart = std::size_t(st.stringOffset) + stringIndex;
                const std::size_t stringLimit = std::size_t(st.stringOffset) + st.stringSize;
                std::size_t stringEnd = stringStart;
                while (stringEnd < stringLimit && bytes[stringEnd] != 0) ++stringEnd;
                if (stringEnd < stringLimit) {
                    symbol.name.assign(
                        reinterpret_cast<const char*>(bytes.data() + stringStart),
                        stringEnd - stringStart);
                }
            }
            image.symbols.push_back(std::move(symbol));
        }
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
