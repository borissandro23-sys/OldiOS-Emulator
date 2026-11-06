#include "armv6.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr std::uint32_t FLAG_N = 1u << 31;
constexpr std::uint32_t FLAG_Z = 1u << 30;
constexpr std::uint32_t FLAG_C = 1u << 29;
constexpr std::uint32_t FLAG_V = 1u << 28;

std::int32_t signExtend24(std::uint32_t value) {
    if ((value & 0x00800000u) != 0) {
        value |= 0xFF000000u;
    }
    return static_cast<std::int32_t>(value);
}
}

Armv6::Armv6() : memory_(kMemorySize, 0) {
    reset();
}

void Armv6::reset() {
    regs_.fill(0);
    cpsr_ = 0;
    halted_ = false;
}

void Armv6::loadProgram(const std::vector<std::uint32_t>& words,
                        std::uint32_t address) {
    if ((address & 3u) != 0) {
        throw std::runtime_error("Program address must be 4-byte aligned.");
    }

    if (static_cast<std::uint64_t>(address) +
            words.size() * sizeof(std::uint32_t) > memory_.size()) {
        throw std::runtime_error("Program does not fit in emulator memory.");
    }

    for (std::size_t i = 0; i < words.size(); ++i) {
        write32(address + static_cast<std::uint32_t>(i * 4), words[i]);
    }

    regs_[15] = address;
    halted_ = false;
}

std::uint32_t Armv6::reg(std::size_t index) const {
    if (index >= regs_.size()) {
        throw std::out_of_range("Register index must be between 0 and 15.");
    }
    return regs_[index];
}

std::uint32_t Armv6::cpsr() const {
    return cpsr_;
}

bool Armv6::halted() const {
    return halted_;
}

std::uint32_t Armv6::read32(std::uint32_t address) const {
    if (static_cast<std::uint64_t>(address) + 4 > memory_.size()) {
        throw std::runtime_error("32-bit memory read out of range.");
    }

    return static_cast<std::uint32_t>(memory_[address]) |
           (static_cast<std::uint32_t>(memory_[address + 1]) << 8) |
           (static_cast<std::uint32_t>(memory_[address + 2]) << 16) |
           (static_cast<std::uint32_t>(memory_[address + 3]) << 24);
}

void Armv6::write32(std::uint32_t address, std::uint32_t value) {
    if (static_cast<std::uint64_t>(address) + 4 > memory_.size()) {
        throw std::runtime_error("32-bit memory write out of range.");
    }

    memory_[address] = static_cast<std::uint8_t>(value);
    memory_[address + 1] = static_cast<std::uint8_t>(value >> 8);
    memory_[address + 2] = static_cast<std::uint8_t>(value >> 16);
    memory_[address + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::uint32_t Armv6::read8(std::uint32_t address) const {
    if (address >= memory_.size()) {
        throw std::runtime_error("8-bit memory read out of range.");
    }
    return memory_[address];
}

void Armv6::write8(std::uint32_t address, std::uint8_t value) {
    if (address >= memory_.size()) {
        throw std::runtime_error("8-bit memory write out of range.");
    }
    memory_[address] = value;
}

bool Armv6::conditionPassed(std::uint32_t condition) const {
    const bool n = (cpsr_ & FLAG_N) != 0;
    const bool z = (cpsr_ & FLAG_Z) != 0;
    const bool c = (cpsr_ & FLAG_C) != 0;
    const bool v = (cpsr_ & FLAG_V) != 0;

    switch (condition) {
    case 0x0: return z;                 // EQ
    case 0x1: return !z;                // NE
    case 0x2: return c;                 // CS/HS
    case 0x3: return !c;                // CC/LO
    case 0x4: return n;                 // MI
    case 0x5: return !n;                // PL
    case 0x6: return v;                 // VS
    case 0x7: return !v;                // VC
    case 0x8: return c && !z;           // HI
    case 0x9: return !c || z;           // LS
    case 0xA: return n == v;             // GE
    case 0xB: return n != v;             // LT
    case 0xC: return !z && n == v;       // GT
    case 0xD: return z || n != v;        // LE
    case 0xE: return true;               // AL
    default:  return false;              // NV
    }
}

void Armv6::setNz(std::uint32_t value) {
    cpsr_ &= ~(FLAG_N | FLAG_Z);
    if ((value & 0x80000000u) != 0) cpsr_ |= FLAG_N;
    if (value == 0) cpsr_ |= FLAG_Z;
}

void Armv6::execute(std::uint32_t ins, std::uint32_t address) {
    // Prototype-only stop marker used by the built-in demo.
    if (ins == 0xFFFFFFFFu) {
        halted_ = true;
        return;
    }
    const std::uint32_t condition = ins >> 28;
    if (!conditionPassed(condition)) {
        return;
    }

    // B / BL
    if ((ins & 0x0E000000u) == 0x0A000000u) {
        const bool link = (ins & (1u << 24)) != 0;
        if (link) regs_[14] = address + 4;
        const auto offset = signExtend24(ins & 0x00FFFFFFu) * 4;
        regs_[15] = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(address) + 8 + offset);
        return;
    }

    // Single data transfer: immediate-offset LDR / STR, word or byte.
    if ((ins & 0x0C000000u) == 0x04000000u) {
        const bool registerOffset = (ins & (1u << 25)) != 0;
        const bool preIndex = (ins & (1u << 24)) != 0;
        const bool addOffset = (ins & (1u << 23)) != 0;
        const bool byteTransfer = (ins & (1u << 22)) != 0;
        const bool writeBack = (ins & (1u << 21)) != 0;
        const bool load = (ins & (1u << 20)) != 0;
        const unsigned rn = (ins >> 16) & 0xF;
        const unsigned rd = (ins >> 12) & 0xF;

        if (registerOffset) {
            throw std::runtime_error("Register-offset LDR/STR is not implemented yet.");
        }

        const std::uint32_t base = rn == 15 ? address + 8 : regs_[rn];
        const std::uint32_t offset = ins & 0xFFFu;
        const std::uint32_t adjusted = addOffset ? base + offset : base - offset;
        const std::uint32_t effective = preIndex ? adjusted : base;

        if (load) {
            regs_[rd] = byteTransfer ? read8(effective) : read32(effective);
        } else {
            if (byteTransfer) {
                write8(effective, static_cast<std::uint8_t>(regs_[rd]));
            } else {
                write32(effective, regs_[rd]);
            }
        }

        if (!preIndex || writeBack) regs_[rn] = adjusted;
        return;
    }

    // Data-processing instructions. This first version supports immediate
    // operands and unshifted register operands.
    if ((ins & 0x0C000000u) == 0) {
        const bool immediate = (ins & (1u << 25)) != 0;
        const bool setFlags = (ins & (1u << 20)) != 0;
        const unsigned opcode = (ins >> 21) & 0xF;
        const unsigned rn = (ins >> 16) & 0xF;
        const unsigned rd = (ins >> 12) & 0xF;

        std::uint32_t operand2 = 0;
        if (immediate) {
            const unsigned rotate = ((ins >> 8) & 0xF) * 2;
            const std::uint32_t value = ins & 0xFFu;
            operand2 = rotate == 0 ? value :
                ((value >> rotate) | (value << (32 - rotate)));
        } else {
            if ((ins & 0xFF0u) != 0) {
                throw std::runtime_error("Shifted register operands are not implemented yet.");
            }
            const unsigned rm = ins & 0xF;
            operand2 = rm == 15 ? address + 8 : regs_[rm];
        }

        const std::uint32_t left = rn == 15 ? address + 8 : regs_[rn];
        std::uint32_t result = 0;
        bool writesResult = true;
        bool arithmetic = false;
        bool carry = (cpsr_ & FLAG_C) != 0;
        bool overflow = (cpsr_ & FLAG_V) != 0;

        switch (opcode) {
        case 0x0: result = left & operand2; break; // AND
        case 0x1: result = left ^ operand2; break; // EOR
        case 0x2: // SUB
        case 0xA: // CMP
            result = left - operand2;
            arithmetic = true;
            carry = left >= operand2;
            overflow = (((left ^ operand2) & (left ^ result)) >> 31) != 0;
            if (opcode == 0xA) writesResult = false;
            break;
        case 0x4: // ADD
            result = left + operand2;
            arithmetic = true;
            carry = result < left;
            overflow = ((~(left ^ operand2) & (left ^ result)) >> 31) != 0;
            break;
        case 0xC: result = left | operand2; break; // ORR
        case 0xD: result = operand2; break;         // MOV
        case 0xE: result = left & ~operand2; break; // BIC
        case 0xF: result = ~operand2; break;        // MVN
        default:
            throw std::runtime_error("Unsupported ARM data-processing opcode.");
        }

        if (writesResult) {
            if (rd == 15) {
                regs_[15] = result & ~3u;
            } else {
                regs_[rd] = result;
            }
        }

        if (setFlags || !writesResult) {
            setNz(result);
            if (arithmetic) {
                cpsr_ &= ~(FLAG_C | FLAG_V);
                if (carry) cpsr_ |= FLAG_C;
                if (overflow) cpsr_ |= FLAG_V;
            }
        }
        return;
    }

    throw std::runtime_error("Unsupported ARM instruction at address " +
                             std::to_string(address));
}

void Armv6::run(std::size_t instructionLimit) {
    std::size_t executed = 0;
    while (!halted_ && executed < instructionLimit) {
        const std::uint32_t address = regs_[15];
        const std::uint32_t instruction = read32(address);
        regs_[15] = address + 4;
        execute(instruction, address);
        ++executed;
    }

    if (executed == instructionLimit) {
        std::cout << "Instruction limit reached; stopping safely.\n";
    }
}
