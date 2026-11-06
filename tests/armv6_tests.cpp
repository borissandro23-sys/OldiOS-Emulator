#include "armv6.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    Armv6 cpu;
    cpu.loadProgram({
        0xE3A00005u, // MOV r0, #5
        0xE3A01007u, // MOV r1, #7
        0xE0902001u, // ADDS r2, r0, r1
        0xFFFFFFFFu  // test halt marker
    });
    cpu.run(16);

    if (!cpu.halted() || cpu.reg(0) != 5 || cpu.reg(1) != 7 ||
        cpu.reg(2) != 12 || (cpu.cpsr() & (1u << 30)) != 0) {
        std::cerr << "ARMv6 smoke test failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "ARMv6 smoke test passed\n";
    return EXIT_SUCCESS;
}
