#include "armv6.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\\n';
        ++failures;
    }
}

void testImmediateMoveAndAdd() {
    Armv6 cpu;
    cpu.loadProgram({0xE3A00007u, // MOV r0, #7
                     0xE2801005u, // ADD r1, r0, #5
                     0xFFFFFFFFu}, 0x1000u);
    cpu.step();
    cpu.step();
    check(cpu.reg(0) == 7u, "MOV immediate should set r0 to 7");
    check(cpu.reg(1) == 12u, "ADD immediate should set r1 to 12");
}

void testMultiplyIsRejected() {
    Armv6 cpu;
    cpu.loadProgram({0xE0000291u}, 0x1000u); // MUL r0, r1, r2
    bool rejected = false;
    try {
        cpu.step();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    check(rejected, "MUL must be rejected, not misdecoded as data processing");
}

void testExtraLoadStoreIsRejected() {
    Armv6 cpu;
    cpu.loadProgram({0xE1D000B0u}, 0x1000u); // LDRH r0, [r0]
    bool rejected = false;
    try {
        cpu.step();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    check(rejected, "LDRH must be rejected until extra load/store is implemented");
}
}

int main() {
    testImmediateMoveAndAdd();
    testMultiplyIsRejected();
    testExtraLoadStoreIsRejected();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\\n";
        return EXIT_FAILURE;
    }
    std::cout << "All ARMv6 decoder tests passed.\\n";
    return EXIT_SUCCESS;
}
