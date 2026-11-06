#import "OldiOSCoreBridge.h"

#include "armv6.hpp"
#include "macho_loader.hpp"

#include <exception>
#include <filesystem>
#include <string>
#include <vector>

@implementation OldiOSCoreBridge

+ (NSString *)runtimeStatus {
    try {
        Armv6 cpu;
        cpu.loadProgram(std::vector<std::uint32_t>{
            0xE3A0002Au, // MOV R0, #42
            0xFFFFFFFFu  // prototype stop marker
        }, 0x1000u);
        cpu.step();

        if (cpu.reg(0) != 42u || cpu.reg(15) != 0x1004u) {
            return @"C++ core linked, but ARM self-test failed.";
        }
        return @"C++ core: linked\nARM self-test: passed (R0 = 42)\nMach-O loader: compiled";
    } catch (const std::exception& error) {
        return [NSString stringWithFormat:@"C++ core test failed: %s", error.what()];
    }
}

+ (NSString *)inspectMachOAtPath:(NSString *)path {
    try {
        MachOImage image;
        std::string error;
        const std::filesystem::path executablePath(path.fileSystemRepresentation);
        if (!loadMachOImage(executablePath, image, error)) {
            return [NSString stringWithFormat:@"Mach-O load failed: %s", error.c_str()];
        }
        return [NSString stringWithFormat:
            @"Mach-O parsed\nSegments: %lu\nDependencies: %lu\nEntry point: 0x%08X",
            static_cast<unsigned long>(image.segments.size()),
            static_cast<unsigned long>(image.dependencies.size()),
            image.hasEntryPoint ? image.entryPoint : 0u];
    } catch (const std::exception& error) {
        return [NSString stringWithFormat:@"Mach-O inspection failed: %s", error.what()];
    }
}

@end
