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

    // Legacy Thumb BL uses two halfwords: H1 builds LR, H2 branches.
    Armv6 thumbBl;
    thumbBl.loadProgram({
        0xE3A0C011u, // MOV R12, #0x11
        0xE12FFF3Cu, // BLX R12 -> Thumb address 0x10
        0x00000000u,
        0x00000000u,
        0xF801F000u  // H1=F000 at 0x10, H2=F801 at 0x12
    });
    thumbBl.step();
    thumbBl.step();
    thumbBl.step();
    ok &= expect(thumbBl.reg(14) == 0x14u, "Thumb BL first half must build the high offset in LR");
    thumbBl.step();
    ok &= expect(thumbBl.reg(15) == 0x16u, "Thumb BL second half must branch to the computed target");
    ok &= expect(thumbBl.reg(14) == 0x15u, "Thumb BL must save the Thumb return address in LR");
    ok &= expect((thumbBl.cpsr() & (1u << 5)) != 0, "Thumb BL must remain in Thumb state");

    // ARM register-offset STR/LDR with an unshifted Rm.
    Armv6 registerOffset;
    registerOffset.loadProgram({
        0xE3A01040u, // MOV R1, #0x40
        0xE3A02004u, // MOV R2, #4
        0xE3A03055u, // MOV R3, #0x55
        0xE7813002u, // STR R3, [R1, R2]
        0xE3A03000u, // MOV R3, #0
        0xE7913002u  // LDR R3, [R1, R2]
    });
    for (int i = 0; i < 6; ++i) registerOffset.step();
    ok &= expect(registerOffset.reg(3) == 0x55u,
                 "ARM register-offset LDR must read from base plus Rm");
    
    // PC-relative register-offset LDR: base is current instruction address + 8.
    Armv6 pcRegisterOffset;
    pcRegisterOffset.loadProgram({
        0xE3A0C004u, // MOV R12, #4
        0xE79F300Cu, // LDR R3, [PC, R12] -> address 0x10
        0xE1A00000u, // NOP
        0xE1A00000u, // NOP
        0x12345678u  // data at address 0x10
    });
    pcRegisterOffset.step();
    pcRegisterOffset.step();
    ok &= expect(pcRegisterOffset.reg(3) == 0x12345678u,
                 "ARM register-offset LDR must use address+8 when Rn is PC");

    // MOV R0, #0x10; BX R0. An even target selects ARM state.
    Armv6 bx;
    bx.loadProgram({0xE3A00010u, 0xE12FFF10u});
    bx.step();
    bx.step();
    ok &= expect(bx.reg(15) == 0x10u, "BX to an even target must word-align PC");
    ok &= expect((bx.cpsr() & (1u << 5)) == 0, "BX even target must select ARM state");

    // A Mach-O ARM_THREAD_STATE can start directly in Thumb mode.
    MachOImage thumbImage;
    thumbImage.hasEntryPoint = true;
    thumbImage.hasThreadState = true;
    thumbImage.entryPoint = 0x1000u;
    thumbImage.initialRegisters[0] = 42u;
    thumbImage.initialCpsr = 1u << 5;
    MachOSegment textSegment;
    textSegment.name = "__TEXT";
    textSegment.vmAddress = 0x1000u;
    textSegment.vmSize = 2u;
    textSegment.memory = {0x07u, 0x20u}; // MOV R0, #7
    thumbImage.segments.push_back(textSegment);
    Armv6 fromMachO;
    fromMachO.loadImage(thumbImage);
    ok &= expect(fromMachO.reg(0) == 42u, "Mach-O initial general registers should be restored");
    ok &= expect((fromMachO.cpsr() & (1u << 5)) != 0, "Mach-O CPSR should select Thumb mode");
    fromMachO.step();
    ok &= expect(fromMachO.reg(0) == 7u, "Mach-O Thumb entry point should execute");

    if (!ok) return 1;
    std::cout << "ARMv6 branch-exchange tests passed.\n";
    return 0;
}
