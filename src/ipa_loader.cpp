#include "ipa_loader.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace {
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

bool hasSuffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}
} // namespace

bool inspectIpa(const std::filesystem::path& filePath, IpaInfo& result) {
    result = {};
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        result.error = "Cannot open the selected file.";
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 22 || length > static_cast<std::streamoff>(512) * 1024 * 1024) {
        result.error = "File size is invalid or exceeds the 512 MiB safety limit.";
        return false;
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
    if (!input.read(reinterpret_cast<char*>(data.data()), length)) {
        result.error = "Could not read the complete IPA file.";
        return false;
    }

    // Find the ZIP End Of Central Directory record in the final 65,557 bytes.
    const std::size_t searchStart = data.size() > 65557 ? data.size() - 65557 : 0;
    std::size_t eocd = data.size();
    for (std::size_t pos = data.size() - 22;; --pos) {
        if (read32(data, pos) == 0x06054b50u) {
            eocd = pos;
            break;
        }
        if (pos == searchStart) break;
    }
    if (eocd == data.size()) {
        result.error = "ZIP directory record was not found. This is not a supported IPA archive.";
        return false;
    }

    const std::uint16_t diskNumber = read16(data, eocd + 4);
    const std::uint16_t centralDisk = read16(data, eocd + 6);
    const std::uint16_t entriesOnDisk = read16(data, eocd + 8);
    const std::uint16_t totalEntries = read16(data, eocd + 10);
    const std::uint32_t centralSize = read32(data, eocd + 12);
    const std::uint32_t centralOffset = read32(data, eocd + 16);
    const std::uint16_t commentLength = read16(data, eocd + 20);

    if (diskNumber != 0 || centralDisk != 0 || entriesOnDisk != totalEntries ||
        totalEntries == 0xffffu || centralOffset == 0xffffffffu ||
        centralSize == 0xffffffffu) {
        result.error = "Multi-disk and ZIP64 IPA archives are not supported yet.";
        return false;
    }
    if (eocd + 22u + commentLength > data.size() ||
        static_cast<std::uint64_t>(centralOffset) + centralSize > data.size()) {
        result.error = "ZIP directory points outside the file.";
        return false;
    }

    std::size_t cursor = centralOffset;
    const std::size_t centralEnd = static_cast<std::size_t>(centralOffset) + centralSize;
    for (std::uint32_t index = 0; index < totalEntries; ++index) {
        if (cursor + 46 > centralEnd || read32(data, cursor) != 0x02014b50u) {
            result.error = "The ZIP central directory is damaged.";
            return false;
        }

        const std::uint32_t compressedSize = read32(data, cursor + 20);
        const std::uint32_t uncompressedSize = read32(data, cursor + 24);
        const std::uint16_t nameLength = read16(data, cursor + 28);
        const std::uint16_t extraLength = read16(data, cursor + 30);
        const std::uint16_t entryCommentLength = read16(data, cursor + 32);
        const std::uint16_t diskStart = read16(data, cursor + 34);
        const std::uint32_t localHeaderOffset = read32(data, cursor + 42);
        const std::size_t recordLength = 46u + nameLength + extraLength + entryCommentLength;

        if (cursor + recordLength > centralEnd || diskStart != 0 ||
            compressedSize == 0xffffffffu || uncompressedSize == 0xffffffffu ||
            localHeaderOffset == 0xffffffffu) {
            result.error = "Unsupported or invalid ZIP directory entry.";
            return false;
        }

        std::string name(reinterpret_cast<const char*>(data.data() + cursor + 46), nameLength);
        for (char& ch : name) if (ch == '\\') ch = '/';

        if (name.rfind("Payload/", 0) == 0 && hasSuffix(name, ".app/Info.plist")) {
            const std::size_t appEnd = name.find(".app/");
            if (appEnd != std::string::npos) {
                result.appBundlePath = name.substr(0, appEnd + 4);
                result.infoPlistPath = name;
            }
        }

        ++result.fileCount;
        cursor += recordLength;
    }

    if (result.infoPlistPath.empty()) {
        result.error = "ZIP archive is readable, but Payload/<App>.app/Info.plist was not found.";
        return false;
    }

    result.valid = true;
    return true;
}
