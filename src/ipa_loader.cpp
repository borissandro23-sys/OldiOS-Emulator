#include "ipa_loader.hpp"
#include "miniz.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
constexpr std::uint64_t MAX_IPA_SIZE = 512ull * 1024 * 1024;
constexpr std::uint64_t MAX_EXECUTABLE_SIZE = 256ull * 1024 * 1024;
constexpr std::uint32_t ZIP_EOCD = 0x06054b50u;
constexpr std::uint32_t ZIP_CENTRAL = 0x02014b50u;
constexpr std::uint32_t ZIP_LOCAL = 0x04034b50u;

std::uint16_t read16(const std::vector<std::uint8_t>& data, std::size_t at) {
    return static_cast<std::uint16_t>(data[at]) |
           (static_cast<std::uint16_t>(data[at + 1]) << 8);
}

std::uint32_t read32(const std::vector<std::uint8_t>& data, std::size_t at) {
    return static_cast<std::uint32_t>(data[at]) |
           (static_cast<std::uint32_t>(data[at + 1]) << 8) |
           (static_cast<std::uint32_t>(data[at + 2]) << 16) |
           (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

std::uint32_t read32be(const std::vector<std::uint8_t>& data, std::size_t at) {
    return (static_cast<std::uint32_t>(data[at]) << 24) |
           (static_cast<std::uint32_t>(data[at + 1]) << 16) |
           (static_cast<std::uint32_t>(data[at + 2]) << 8) |
           static_cast<std::uint32_t>(data[at + 3]);
}

struct ZipEntry {
    std::string name;
    std::uint16_t flags = 0;
    std::uint16_t method = 0;
    std::uint32_t crc = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t uncompressedSize = 0;
    std::uint32_t localHeaderOffset = 0;
};

bool extractEntry(const std::vector<std::uint8_t>& archive,
                  const ZipEntry& entry,
                  std::vector<std::uint8_t>& output,
                  std::string& error) {
    if ((entry.flags & 1u) != 0) {
        error = "Encrypted ZIP entries are not supported.";
        return false;
    }
    if (entry.uncompressedSize > MAX_EXECUTABLE_SIZE ||
        entry.compressedSize > MAX_IPA_SIZE) {
        error = "The selected ZIP entry exceeds the extraction safety limit.";
        return false;
    }

    const std::size_t local = entry.localHeaderOffset;
    if (local > archive.size() || archive.size() - local < 30 ||
        read32(archive, local) != ZIP_LOCAL) {
        error = "The executable's ZIP local header is invalid.";
        return false;
    }

    const std::uint16_t nameLength = read16(archive, local + 26);
    const std::uint16_t extraLength = read16(archive, local + 28);
    const std::uint64_t dataOffset64 =
        static_cast<std::uint64_t>(local) + 30u + nameLength + extraLength;
    if (dataOffset64 > archive.size() ||
        entry.compressedSize > archive.size() - static_cast<std::size_t>(dataOffset64)) {
        error = "The executable data points outside the IPA archive.";
        return false;
    }

    const auto* compressed = archive.data() + static_cast<std::size_t>(dataOffset64);
    output.assign(entry.uncompressedSize, 0);

    if (entry.method == 0) {
        if (entry.compressedSize != entry.uncompressedSize) {
            error = "The stored ZIP entry has inconsistent sizes.";
            return false;
        }
        std::copy(compressed, compressed + entry.compressedSize, output.begin());
    } else if (entry.method == 8) {
        mz_stream stream{};
        stream.next_in = compressed;
        stream.avail_in = entry.compressedSize;
        stream.next_out = output.empty() ? nullptr : output.data();
        stream.avail_out = entry.uncompressedSize;
        if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
            error = "Could not initialize the ZIP deflate decoder.";
            return false;
        }
        const int status = mz_inflate(&stream, MZ_FINISH);
        mz_inflateEnd(&stream);
        if (status != MZ_STREAM_END || stream.total_out != entry.uncompressedSize) {
            error = "The executable could not be decompressed (invalid deflate stream).";
            return false;
        }
    } else {
        error = "Unsupported ZIP compression method: " + std::to_string(entry.method);
        return false;
    }

    const auto actualCrc = static_cast<std::uint32_t>(
        mz_crc32(MZ_CRC32_INIT, output.empty() ? nullptr : output.data(), output.size()));
    if (actualCrc != entry.crc) {
        error = "The extracted file failed its ZIP CRC check.";
        return false;
    }
    return true;
}

std::string plistExecutableName(const std::vector<std::uint8_t>& bytes) {
    const std::string plist(bytes.begin(), bytes.end());
    const std::string key = "<key>CFBundleExecutable</key>";
    const auto keyAt = plist.find(key);
    if (keyAt == std::string::npos) return {};
    const auto open = plist.find("<string>", keyAt + key.size());
    if (open == std::string::npos) return {};
    const auto start = open + 8;
    const auto end = plist.find("</string>", start);
    if (end == std::string::npos || end == start) return {};
    std::string value = plist.substr(start, end - start);
    if (value.find('/') != std::string::npos ||
        value.find('\\') != std::string::npos ||
        value == "." || value == "..") return {};
    return value;
}

bool parseMachO(const std::vector<std::uint8_t>& bytes, IpaInfo& result) {
    if (bytes.size() < 28) {
        result.error = "The extracted executable is too small to contain a Mach-O header.";
        return false;
    }

    const std::uint32_t magic = read32(bytes, 0);
    bool bigEndian = false;
    if (magic == 0xfeedfaceu) {
        bigEndian = false;
    } else if (magic == 0xcefaedfeu) {
        bigEndian = true;
    } else if (magic == 0xcafebabeu || magic == 0xbebafecau) {
        result.error = "This executable is a universal (fat) Mach-O file; fat-binary selection is not implemented yet.";
        return false;
    } else {
        result.error = "The app executable is not a supported 32-bit Mach-O file.";
        return false;
    }

    const auto readHeader32 = [&](std::size_t offset) {
        return bigEndian ? read32be(bytes, offset) : read32(bytes, offset);
    };
    result.cpuType = readHeader32(4);
    result.cpuSubtype = readHeader32(8);
    result.fileType = readHeader32(12);

    if (result.cpuType != 12u) {
        result.error = "Mach-O was found, but its CPU type is not 32-bit ARM (CPU_TYPE_ARM).";
        return false;
    }
    if (result.fileType != 2u) {
        result.error = "Mach-O is not an executable image (MH_EXECUTE).";
        return false;
    }
    result.executableSize = bytes.size();
    return true;
}

bool safeFileName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
    });
}
} // namespace

bool loadIpaExecutable(const std::filesystem::path& filePath, IpaInfo& result) {
    result = {};
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        result.error = "Cannot open the selected IPA file.";
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 22 || static_cast<std::uint64_t>(length) > MAX_IPA_SIZE) {
        result.error = "IPA size is invalid or exceeds the 512 MiB safety limit.";
        return false;
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> archive(static_cast<std::size_t>(length));
    if (!input.read(reinterpret_cast<char*>(archive.data()), length)) {
        result.error = "Could not read the complete IPA file.";
        return false;
    }

    const std::size_t searchStart = archive.size() > 65557 ? archive.size() - 65557 : 0;
    std::size_t eocd = archive.size();
    for (std::size_t pos = archive.size() - 22;; --pos) {
        if (read32(archive, pos) == ZIP_EOCD) {
            eocd = pos;
            break;
        }
        if (pos == searchStart) break;
    }
    if (eocd == archive.size()) {
        result.error = "ZIP end-of-directory record was not found.";
        return false;
    }

    const std::uint16_t disk = read16(archive, eocd + 4);
    const std::uint16_t centralDisk = read16(archive, eocd + 6);
    const std::uint16_t diskEntries = read16(archive, eocd + 8);
    const std::uint16_t totalEntries = read16(archive, eocd + 10);
    const std::uint32_t centralSize = read32(archive, eocd + 12);
    const std::uint32_t centralOffset = read32(archive, eocd + 16);
    const std::uint16_t commentLength = read16(archive, eocd + 20);

    if (disk != 0 || centralDisk != 0 || diskEntries != totalEntries ||
        totalEntries == 0xffffu || centralOffset == 0xffffffffu ||
        centralSize == 0xffffffffu) {
        result.error = "Multi-disk and ZIP64 IPA archives are not supported yet.";
        return false;
    }
    if (eocd + 22u + commentLength > archive.size() ||
        static_cast<std::uint64_t>(centralOffset) + centralSize > archive.size()) {
        result.error = "ZIP central directory points outside the IPA file.";
        return false;
    }

    std::vector<ZipEntry> entries;
    entries.reserve(totalEntries);
    std::size_t cursor = centralOffset;
    const std::size_t centralEnd = static_cast<std::size_t>(centralOffset) + centralSize;
    for (std::uint32_t index = 0; index < totalEntries; ++index) {
        if (cursor > centralEnd || centralEnd - cursor < 46 ||
            read32(archive, cursor) != ZIP_CENTRAL) {
            result.error = "The ZIP central directory is damaged.";
            return false;
        }

        ZipEntry entry;
        entry.flags = read16(archive, cursor + 8);
        entry.method = read16(archive, cursor + 10);
        entry.crc = read32(archive, cursor + 16);
        entry.compressedSize = read32(archive, cursor + 20);
        entry.uncompressedSize = read32(archive, cursor + 24);
        const std::uint16_t nameLength = read16(archive, cursor + 28);
        const std::uint16_t extraLength = read16(archive, cursor + 30);
        const std::uint16_t entryCommentLength = read16(archive, cursor + 32);
        const std::uint16_t diskStart = read16(archive, cursor + 34);
        entry.localHeaderOffset = read32(archive, cursor + 42);
        const std::size_t recordLength =
            46u + nameLength + extraLength + entryCommentLength;

        if (recordLength > centralEnd - cursor || diskStart != 0 ||
            entry.compressedSize == 0xffffffffu ||
            entry.uncompressedSize == 0xffffffffu ||
            entry.localHeaderOffset == 0xffffffffu) {
            result.error = "Unsupported or invalid ZIP directory entry.";
            return false;
        }
        entry.name.assign(reinterpret_cast<const char*>(archive.data() + cursor + 46),
                          nameLength);
        std::replace(entry.name.begin(), entry.name.end(), '\\', '/');

        if (entry.name.rfind("Payload/", 0) == 0 &&
            entry.name.size() > 10 &&
            entry.name.find(".app/") != std::string::npos) {
            const auto appEnd = entry.name.find(".app/");
            const std::string bundle = entry.name.substr(0, appEnd + 4);
            if (result.appBundlePath.empty() &&
                entry.name.compare(appEnd + 5, std::string::npos, "Info.plist") == 0) {
                result.appBundlePath = bundle;
                result.infoPlistPath = entry.name;
            }
        }
        entries.push_back(std::move(entry));
        cursor += recordLength;
    }

    if (result.infoPlistPath.empty()) {
        result.error = "Payload/<App>.app/Info.plist was not found in the IPA.";
        return false;
    }

    std::vector<std::uint8_t> plistBytes;
    const auto plistIt = std::find_if(entries.begin(), entries.end(), [&](const ZipEntry& e) {
        return e.name == result.infoPlistPath;
    });
    if (plistIt == entries.end() ||
        !extractEntry(archive, *plistIt, plistBytes, result.error)) {
        if (result.error.empty()) result.error = "Could not extract Info.plist.";
        return false;
    }

    result.executableName = plistExecutableName(plistBytes);
    if (result.executableName.empty()) {
        const auto slash = result.appBundlePath.find_last_of('/');
        const auto appName = result.appBundlePath.substr(slash == std::string::npos ? 0 : slash + 1);
        result.executableName = appName.substr(0, appName.size() - 4);
    }
    if (!safeFileName(result.executableName)) {
        result.error = "Info.plist contains an unsafe executable name.";
        return false;
    }

    const std::string executableEntryName =
        result.appBundlePath + "/" + result.executableName;
    const auto executableIt = std::find_if(entries.begin(), entries.end(),
        [&](const ZipEntry& e) { return e.name == executableEntryName; });
    if (executableIt == entries.end()) {
        result.error = "The main executable was not found: " + executableEntryName;
        return false;
    }

    std::vector<std::uint8_t> executable;
    if (!extractEntry(archive, *executableIt, executable, result.error)) return false;
    if (!parseMachO(executable, result)) return false;

    std::error_code ec;
    const auto outputDirectory = std::filesystem::temp_directory_path(ec) /
                                 "OldiOS-Emulator";
    if (ec) {
        result.error = "Could not locate the system temporary directory.";
        return false;
    }
    std::filesystem::create_directories(outputDirectory, ec);
    if (ec) {
        result.error = "Could not create the emulator's temporary extraction directory.";
        return false;
    }

    result.executablePath = outputDirectory / result.executableName;
    std::ofstream output(result.executablePath, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char*>(executable.data()),
                                 static_cast<std::streamsize>(executable.size()))) {
        result.error = "Could not write the extracted Mach-O executable to disk.";
        result.executablePath.clear();
        return false;
    }

    result.valid = true;
    return true;
}


bool installIpaAppBundle(const std::filesystem::path& filePath,
                         IpaInfo& result,
                         std::filesystem::path& installedPath,
                         std::string& error) {
    installedPath.clear();
    error.clear();
    if (!loadIpaExecutable(filePath, result)) {
        error = result.error;
        return false;
    }

    std::ifstream input(filePath, std::ios::binary);
    if (!input) { error = "Cannot reopen the IPA file."; return false; }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 22 || static_cast<std::uint64_t>(length) > MAX_IPA_SIZE) {
        error = "IPA size is invalid."; return false;
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> archive(static_cast<std::size_t>(length));
    if (!input.read(reinterpret_cast<char*>(archive.data()), length)) {
        error = "Could not read the IPA file."; return false;
    }

    const std::size_t searchStart = archive.size() > 65557 ? archive.size() - 65557 : 0;
    std::size_t eocd = archive.size();
    for (std::size_t pos = archive.size() - 22;; --pos) {
        if (read32(archive, pos) == ZIP_EOCD) { eocd = pos; break; }
        if (pos == searchStart) break;
    }
    if (eocd == archive.size()) { error = "ZIP directory was not found."; return false; }
    const auto count = read16(archive, eocd + 10);
    const auto centralSize = read32(archive, eocd + 12);
    const auto centralOffset = read32(archive, eocd + 16);
    if (read16(archive, eocd + 4) != 0 || read16(archive, eocd + 6) != 0 ||
        read16(archive, eocd + 8) != count || count == 0xffffu ||
        centralOffset == 0xffffffffu || centralSize == 0xffffffffu ||
        static_cast<std::uint64_t>(centralOffset) + centralSize > archive.size()) {
        error = "Unsupported or invalid ZIP directory."; return false;
    }

    std::vector<ZipEntry> entries;
    std::size_t cursor = centralOffset;
    const std::size_t centralEnd = static_cast<std::size_t>(centralOffset) + centralSize;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (cursor > centralEnd || centralEnd - cursor < 46 ||
            read32(archive, cursor) != ZIP_CENTRAL) {
            error = "The ZIP central directory is damaged."; return false;
        }
        ZipEntry e;
        e.flags = read16(archive, cursor + 8);
        e.method = read16(archive, cursor + 10);
        e.crc = read32(archive, cursor + 16);
        e.compressedSize = read32(archive, cursor + 20);
        e.uncompressedSize = read32(archive, cursor + 24);
        const auto nameLength = read16(archive, cursor + 28);
        const auto extraLength = read16(archive, cursor + 30);
        const auto commentLength = read16(archive, cursor + 32);
        if (read16(archive, cursor + 34) != 0 ||
            e.compressedSize == 0xffffffffu || e.uncompressedSize == 0xffffffffu ||
            read32(archive, cursor + 42) == 0xffffffffu) {
            error = "ZIP64 or multi-disk entries are not supported."; return false;
        }
        const std::size_t recordLength = 46u + nameLength + extraLength + commentLength;
        if (recordLength > centralEnd - cursor) {
            error = "Invalid ZIP entry length."; return false;
        }
        e.localHeaderOffset = read32(archive, cursor + 42);
        e.name.assign(reinterpret_cast<const char*>(archive.data() + cursor + 46), nameLength);
        std::replace(e.name.begin(), e.name.end(), '\\', '/');
        if (e.name.rfind(result.appBundlePath + "/", 0) == 0 ||
            e.name == result.appBundlePath || e.name == result.appBundlePath + "/") {
            entries.push_back(std::move(e));
        }
        cursor += recordLength;
    }
    if (entries.empty()) { error = "No files from Payload/<App>.app were found."; return false; }

    const auto bundleName = std::filesystem::path(result.appBundlePath).filename().string();
    if (!safeFileName(bundleName) || bundleName.size() <= 4 ||
        bundleName.substr(bundleName.size() - 4) != ".app") {
        error = "The app bundle has an unsafe name."; return false;
    }
    std::error_code ec;
    const auto root = std::filesystem::current_path(ec) / "Applications";
    if (ec) { error = "Could not locate the emulator folder."; return false; }
    std::filesystem::create_directories(root, ec);
    if (ec) { error = "Could not create Applications folder: " + ec.message(); return false; }
    const auto destination = root / bundleName;
    if (std::filesystem::exists(destination, ec)) {
        error = "This app is already installed. Remove its folder from Applications before reinstalling.";
        return false;
    }
    const auto staging = root / (bundleName + ".installing");
    std::filesystem::remove_all(staging, ec);
    ec.clear();
    std::filesystem::create_directories(staging, ec);
    if (ec) { error = "Could not create temporary installation folder: " + ec.message(); return false; }

    std::uint64_t totalExtracted = 0;
    for (const auto& entry : entries) {
        const std::string prefix = result.appBundlePath + "/";
        if (entry.name == result.appBundlePath) continue;
        if (entry.name.rfind(prefix, 0) != 0) { error = "Unsafe archive path."; break; }
        const std::string relative = entry.name.substr(prefix.size());
        if (relative.empty()) continue;
        std::filesystem::path relativePath;
        std::size_t start = 0;
        bool unsafe = false;
        while (start <= relative.size()) {
            const auto slash = relative.find('/', start);
            const auto part = relative.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (part == ".." || part == "." || part.empty() ||
                part.find(':') != std::string::npos) { unsafe = true; break; }
            relativePath /= std::filesystem::u8path(part);
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        if (unsafe || relativePath.is_absolute()) { error = "Unsafe path inside IPA bundle."; break; }
        const auto target = staging / relativePath;
        if (entry.name.back() == '/') {
            std::filesystem::create_directories(target, ec);
        } else {
            totalExtracted += entry.uncompressedSize;
            if (entry.uncompressedSize > MAX_EXECUTABLE_SIZE ||
                totalExtracted > MAX_IPA_SIZE) { error = "App bundle exceeds the extraction size limit."; break; }
            std::vector<std::uint8_t> bytes;
            if (!extractEntry(archive, entry, bytes, error)) break;
            std::filesystem::create_directories(target.parent_path(), ec);
            if (!ec) {
                std::ofstream output(target, std::ios::binary | std::ios::trunc);
                if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()))) {
                    error = "Could not write installed app file: " + relative;
                    break;
                }
            }
        }
        if (ec) { error = "Could not create app folder: " + ec.message(); break; }
    }
    if (!error.empty()) {
        std::filesystem::remove_all(staging, ec);
        return false;
    }
    std::filesystem::rename(staging, destination, ec);
    if (ec) {
        std::filesystem::remove_all(staging, ec);
        error = "Could not finish installation: " + ec.message();
        return false;
    }
    installedPath = destination;
    return true;
}
