#include "armv6.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main() {
    MachOImage image;
    image.cpuType = 12u; // CPU_TYPE_ARM
    image.cpuSubtype = 6u; // CPU_SUBTYPE_ARM_V6
    image.fileType = 2u; // MH_EXECUTE
    image.hasEntryPoint = true;
    image.entryPoint = 0x1000u;

    // MOV R0, #42; stop marker used by the current interpreter prototype.
    MachOSegment text;
    text.name = "__TEXT";
    text.vmAddress = 0x1000u;
    text.vmSize = 8u;
    text.fileSize = 8u;
    text.memory = {
        0x2Au, 0x00u, 0xA0u, 0xE3u,
        0xFFu, 0xFFu, 0xFFu, 0xFFu
    };
    image.segments.push_back(text);
    image.totalMappedBytes = text.vmSize;

    bool ok = true;
    Armv6 cpu;
    try {
        cpu.loadImage(image);
        ok &= expect(cpu.reg(15) == image.entryPoint,
                     "PC must be initialized to the Mach-O entry point");
        ok &= expect(cpu.reg(13) != 0 && (cpu.reg(13) & 7u) == 0,
                     "initial stack pointer must be nonzero and 8-byte aligned");
        ok &= expect(cpu.peek32(image.entryPoint) == 0xE3A0002Au,
                     "Mach-O segment bytes must be mapped at their virtual address");

        cpu.step();
        ok &= expect(cpu.reg(0) == 42u,
                     "the first ARM instruction at the Mach-O entry point must execute");
        ok &= expect(cpu.reg(15) == image.entryPoint + 4u,
                     "PC must advance after the first ARM instruction");
    } catch (const std::exception& error) {
        std::cerr << "FAIL: unexpected bootstrap exception: " << error.what() << '\n';
        return 1;
    }

    // Invalid entry points must fail before execution.
    image.entryPoint = 0x2000u;
    bool rejected = false;
    try {
        Armv6 invalid;
        invalid.loadImage(image);
    } catch (const std::exception&) {
        rejected = true;
    }
    ok &= expect(rejected, "an entry point outside mapped segments must be rejected");

    if (!ok) return 1;
    std::cout << "ARMv6 app bootstrap tests passed.\n";
    return 0;
}
