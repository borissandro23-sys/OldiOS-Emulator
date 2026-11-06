#include "armv6.hpp"
#include "display_window.hpp"

#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool parseResolution(const std::string& value, unsigned& width, unsigned& height) {
    const auto separator = value.find_first_of("xX");
    if (separator == std::string::npos) return false;
    try {
        const auto w = std::stoul(value.substr(0, separator));
        const auto h = std::stoul(value.substr(separator + 1));
        if (w < 1 || h < 1 || w > 8192 || h > 8192) return false;
        width = static_cast<unsigned>(w);
        height = static_cast<unsigned>(h);
        return true;
    } catch (...) {
        return false;
    }
}
}

int main(int argc, char** argv) {
    try {
        unsigned screenWidth = 320;
        unsigned screenHeight = 480;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--screen" && i + 1 < argc) {
                if (!parseResolution(argv[++i], screenWidth, screenHeight)) {
                    throw std::runtime_error("Invalid screen size. Use --screen WIDTHxHEIGHT.");
                }
            } else if (arg == "--help") {
                std::cout << "OldiOS-Emulator\\n"
                          << "Usage: oldios-emu [--screen WIDTHxHEIGHT]\\n"
                          << "Default virtual device display: 320x480\\n";
                return 0;
            } else {
                throw std::runtime_error("Unknown argument: " + arg);
            }
        }

        Armv6 cpu;
        const std::vector<std::uint32_t> program = {
            0xE3A00005u, 0xE3A01007u, 0xE0802001u, 0xE3A04C01u,
            0xE5842000u, 0xE3A02000u, 0xE5943000u, 0xE2433002u,
            0xE353000Au, 0xFFFFFFFFu
        };
        cpu.loadProgram(program);
        cpu.run(100);

        std::cout << "\\nOldiOS-Emulator | ARMv6 interpreter prototype\\n";
        for (unsigned i = 0; i < 8; ++i) {
            std::cout << "r" << i << " = 0x"
                      << std::hex << std::setw(8) << std::setfill('0')
                      << cpu.reg(i) << std::dec << '\\n';
        }
        std::cout << "CPSR = 0x" << std::hex << cpu.cpsr() << std::dec << '\\n';
        std::cout << "Expected: r2 = 0, r3 = 10, Z flag = 1\\n";
        std::cout << "Virtual display: " << screenWidth << "x" << screenHeight << "\\n";

        DisplayWindow display(screenWidth, screenHeight);
        return display.run();
    } catch (const std::exception& error) {
        std::cerr << "Emulator error: " << error.what() << '\\n';
        return 1;
    }
}
