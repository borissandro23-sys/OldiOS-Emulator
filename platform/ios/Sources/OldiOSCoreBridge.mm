#import "OldiOSCoreBridge.h"

#include "armv6.hpp"
#include "ipa_loader.hpp"
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
        return @"C++ core: linked\nARM self-test: passed (R0 = 42)\nMach-O loader: compiled\nIPA loader: compiled";
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

+ (NSString *)inspectIPAAtPath:(NSString *)path {
    try {
        IpaInfo ipa;
        const std::filesystem::path ipaPath(path.fileSystemRepresentation);
        if (!loadIpaExecutable(ipaPath, ipa)) {
            return [NSString stringWithFormat:@"IPA analysis failed:\n%s",
                    ipa.error.c_str()];
        }

        MachOImage image;
        std::string error;
        if (!loadMachOImage(ipa.executablePath, image, error)) {
            return [NSString stringWithFormat:@"IPA extracted, but Mach-O parsing failed:\n%s",
                    error.c_str()];
        }

        // Verify that the parsed image can be mapped into the ARM address space.
        // This prepares the initial CPU state but deliberately does not execute app code.
        Armv6 cpu;
        cpu.loadImage(image);
        const std::uint32_t firstInstruction = cpu.peek32(cpu.reg(15));
        const bool thumbMode = (cpu.cpsr() & (1u << 5)) != 0;

        std::string dependencies;
        for (const auto& dependency : image.dependencies) {
            dependencies += "• " + dependency + "\\n";
        }
        if (dependencies.empty()) dependencies = "Не указаны\\n";

        return [NSString stringWithFormat:
            @"IPA успешно разобран\\nApp bundle: %s\\nExecutable: %s\\nРазмер Mach-O: %llu байт\\nCPU type: 0x%08X\\nCPU subtype: 0x%08X\\nСегменты: %lu\\nЗависимости (%lu):\\n%s\\nТочка входа: 0x%08X\\nНачальное состояние: загружено\\nLC_UNIXTHREAD: %@\\nРежим CPU: %@\\nPC: 0x%08X\\nSP: 0x%08X\\nCPSR: 0x%08X\\nПервая инструкция: 0x%08X\\nВыполнение кода: НЕ ЗАПУСКАЛОСЬ",
            ipa.appBundlePath.c_str(),
            ipa.executableName.c_str(),
            static_cast<unsigned long long>(ipa.executableSize),
            ipa.cpuType,
            ipa.cpuSubtype,
            static_cast<unsigned long>(image.segments.size()),
            static_cast<unsigned long>(image.dependencies.size()),
            dependencies.c_str(),
            image.entryPoint,
            image.hasInitialThreadState ? @"найден" : @"отсутствует (резервное состояние)",
            thumbMode ? @"Thumb" : @"ARM",
            cpu.reg(15),
            cpu.reg(13),
            cpu.cpsr(),
            firstInstruction];
    } catch (const std::exception& error) {
        return [NSString stringWithFormat:@"IPA inspection failed: %s", error.what()];
    }
}

@end
