#include "armv6.hpp"

#include <exception>
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
    try {
        Armv6 cpu;

        // Demo program (ARM machine-code words):
        // MOV r0, #5
        // MOV r1, #7
        // ADD r2, r0, r1
        // MOV r4, #0x100
        // STR r2, [r4]
        // MOV r2, #0
        // LDR r3, [r4]
        // SUB r3, r3, #2
        // CMP r3, #10
        const std::vector<std::uint32_t> program = {
            0xE3A00005u,
            0xE3A01007u,
            0xE0802001u,
            0xE3A04C01u,
            0xE5842000u,
            0xE3A02000u,
            0xE5943000u,
            0xE2433002u,
            0xE353000Au,
            0xFFFFFFFFu // demo stop marker (not an ARM instruction)
        };

        cpu.loadProgram(program);
        cpu.run(100);

        std::cout << "\nOldiOS-Emulator | ARMv6 interpreter prototype\n";
        for (unsigned i = 0; i < 8; ++i) {
            std::cout << "r" << i << " = 0x"
                      << std::hex << std::setw(8) << std::setfill('0')
                      << cpu.reg(i) << std::dec << '\n';
        }

        std::cout << "CPSR = 0x" << std::hex << cpu.cpsr() << std::dec << '\n';
        std::cout << "Expected: r2 = 0, r3 = 10, Z flag = 1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Emulator error: " << error.what() << '\n';
        return 1;
    }
}
