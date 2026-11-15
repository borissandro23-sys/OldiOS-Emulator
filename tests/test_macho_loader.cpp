#include "macho_loader.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::vector<std::uint8_t> makeMachO(std::uint32_t fileOffset = 84,
                                    std::uint32_t fileSize = 4,
                                    std::uint32_t vmSize = 4,
                                    std::uint32_t commandSize = 56) {
    std::vector<std::uint8_t> bytes(88, 0);
    put32(bytes, 0, 0xfeedfaceu); // MH_MAGIC
    put32(bytes, 4, 12u);         // CPU_TYPE_ARM
    put32(bytes, 8, 6u);          // CPU_SUBTYPE_ARM_V6
    put32(bytes, 12, 2u);         // MH_EXECUTE
    put32(bytes, 16, 1u);         // ncmds
    put32(bytes, 20, 56u);        // sizeofcmds
    put32(bytes, 28, 1u);         // LC_SEGMENT
    put32(bytes, 32, commandSize);
    const char name[] = "__TEXT";
    for (std::size_t i = 0; i < sizeof(name) - 1; ++i)
        bytes[36 + i] = static_cast<std::uint8_t>(name[i]);
    put32(bytes, 52, 0x1000u);    // vmaddr
    put32(bytes, 56, vmSize);
    put32(bytes, 60, fileOffset);
    put32(bytes, 64, fileSize);
    bytes[84] = 0x11;
    bytes[85] = 0x22;
    bytes[86] = 0x33;
    bytes[87] = 0x44;
    return bytes;
}

bool writeFixture(const std::filesystem::path& path,
                  const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file && file.write(reinterpret_cast<const char*>(bytes.data()),
                              static_cast<std::streamsize>(bytes.size()));
}

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() / "oldios_macho_loader_test.bin";
    bool ok = true;
    MachOImage image;
    std::string error;

    ok &= expect(writeFixture(path, makeMachO()), "write valid Mach-O fixture");
    ok &= expect(loadMachOImage(path, image, error), "valid ARM Mach-O should load");
    ok &= expect(image.cpuType == 12u, "CPU type should be ARM");
    ok &= expect(image.fileType == 2u, "file type should be MH_EXECUTE");
    ok &= expect(image.segments.size() == 1, "one segment should be mapped");
    ok &= expect(image.totalMappedBytes == 4, "mapped byte count should match vmSize");
    ok &= expect(image.segments.size() == 1 && image.segments[0].memory.size() == 4,
                 "segment memory should have vmSize bytes");
    ok &= expect(image.segments.size() == 1 && image.segments[0].memory[0] == 0x11 &&
                 image.segments[0].memory[3] == 0x44,
                 "segment bytes should be copied from the file");

    ok &= expect(writeFixture(path, makeMachO(87, 4, 4)),
                 "write out-of-bounds fixture");
    ok &= expect(!loadMachOImage(path, image, error),
                 "segment extending beyond file should be rejected");
    ok &= expect(!error.empty(), "invalid segment should report an error");

    ok &= expect(writeFixture(path, makeMachO(84, 4, 4, 4)),
                 "write malformed-command fixture");
    ok &= expect(!loadMachOImage(path, image, error),
                 "undersized LC_SEGMENT should be rejected");

    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (!ok) return 1;
    std::cout << "Mach-O loader tests passed.\n";
    return 0;
}
