#include "armv6.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {
bool expect(bool value, const char* message) {
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}
}

int main() {
    bool ok = true;

    // MOV R12, #0x11; BLX R12. Bit 0 selects Thumb and is cleared from PC.
    Armv6 blx;
    blx.loadProgram({0xE3A0C011u, 0xE12FFF3Cu});
    blx.step();
    blx.step();
    ok &= expect(blx.reg(14) == 8u, "BLX must save the return address in LR");
    ok &= expect(blx.reg(15) == 0x10u, "BLX to an odd target must clear bit 0");
    ok &= expect((blx.cpsr() & (1u << 5)) != 0, "BLX odd target must select Thumb state");

    // ARM setup, BLX into Thumb, ADDS R6,R1,#0, PUSH {R4-R7,LR}.
    // The two Thumb halfwords are packed little-endian into one test word.
    Armv6 thumb;
    thumb.loadProgram({
        0xE3A01007u, // MOV R1, #7
        0xE3A0D080u, // MOV SP, #0x80
        0xE3A0C011u, // MOV R12, #0x11
        0xE12FFF3Cu, // BLX R12 -> Thumb address 0x10
        0xB5F01C0Eu  // ADDS R6,R1,#0 ; PUSH {R4-R7,LR}
    });
    for (int i = 0; i < 6; ++i) thumb.step();
    ok &= expect(thumb.reg(6) == 7u, "Thumb ADDS immediate must update the destination");
    ok &= expect(thumb.reg(13) == 0x6Cu, "Thumb PUSH must decrement SP by the register-list size");
    ok &= expect(thumb.peek32(0x7Cu) == 16u, "Thumb PUSH must save LR at the top of the stack");
    ok &= expect((thumb.cpsr() & (1u << 5)) != 0, "Thumb execution must remain in Thumb state");

    // MOV R0, #0x10; BX R0. An even target selects ARM state.
    Armv6 bx;
    bx.loadProgram({0xE3A00010u, 0xE12FFF10u});
    bx.step();
    bx.step();
    ok &= expect(bx.reg(15) == 0x10u, "BX to an even target must word-align PC");
    ok &= expect((bx.cpsr() & (1u << 5)) == 0, "BX even target must select ARM state");

    if (!ok) return 1;
    std::cout << "ARMv6 branch-exchange tests passed.\n";
    return 0;
}
