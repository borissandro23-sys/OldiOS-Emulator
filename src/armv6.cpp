#include "armv6.hpp"

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
constexpr std::uint32_t FLAG_N = 1u << 31;
constexpr std::uint32_t FLAG_Z = 1u << 30;
constexpr std::uint32_t FLAG_C = 1u << 29;
constexpr std::uint32_t FLAG_V = 1u << 28;
constexpr std::uint32_t FLAG_T = 1u << 5;
constexpr std::uint64_t MAX_IMAGE_MEMORY = 256ull * 1024 * 1024;
constexpr std::uint64_t STACK_SIZE = 64ull * 1024;

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
    trace_.clear();
}

void Armv6::loadProgram(const std::vector<std::uint32_t>& words,
                        std::uint32_t address) {
    if ((address & 3u) != 0) {
        throw std::runtime_error("Program address must be 4-byte aligned.");
    }

    memoryBase_ = 0;
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


void Armv6::loadImage(const MachOImage& image) {
    if (image.segments.empty() || !image.hasEntryPoint)
        throw std::runtime_error("Mach-O image has no loadable segments or entry point.");

    std::uint64_t low = UINT64_MAX, high = 0;
    for (const auto& segment : image.segments) {
        if (!segment.vmSize || segment.memory.size() != segment.vmSize)
            throw std::runtime_error("Mach-O segment has an invalid memory buffer.");
        const std::uint64_t begin = segment.vmAddress;
        const std::uint64_t end = begin + segment.vmSize;
        if (end > (std::uint64_t{1} << 32) || end <= begin)
            throw std::runtime_error("Mach-O segment address range overflows.");
        low = std::min(low, begin);
        high = std::max(high, end);
    }
    if (high - low > MAX_IMAGE_MEMORY || high + STACK_SIZE > 0x100000000ull)
        throw std::runtime_error("Mach-O image and stack exceed the supported memory range.");

    for (std::size_t i = 0; i < image.segments.size(); ++i) {
        const std::uint64_t a0 = image.segments[i].vmAddress;
        const std::uint64_t a1 = a0 + image.segments[i].vmSize;
        for (std::size_t j = i + 1; j < image.segments.size(); ++j) {
            const std::uint64_t b0 = image.segments[j].vmAddress;
            const std::uint64_t b1 = b0 + image.segments[j].vmSize;
            if (a0 < b1 && b0 < a1)
                throw std::runtime_error("Mach-O segments overlap in virtual memory.");
        }
    }

    const bool entryMapped = std::any_of(image.segments.begin(), image.segments.end(),
        [&](const MachOSegment& segment) {
            return image.entryPoint >= segment.vmAddress &&
                std::uint64_t(image.entryPoint) + 4 <=
                    std::uint64_t(segment.vmAddress) + segment.vmSize;
        });
    if (!entryMapped || (image.entryPoint & 3u))
        throw std::runtime_error("Mach-O entry point is not a mapped, aligned ARM instruction.");

    imageSections_ = image.sections;
    imageSymbols_ = image.symbols;
    indirectSymbols_ = image.dynamicSymbolTable.indirectSymbols;

    memoryBase_ = static_cast<std::uint32_t>(low);
    memory_.assign(static_cast<std::size_t>(high - low + STACK_SIZE), 0);
    for (const auto& segment : image.segments) {
        const auto offset = static_cast<std::size_t>(std::uint64_t(segment.vmAddress) - low);
        std::copy(segment.memory.begin(), segment.memory.end(), memory_.begin() + offset);
    }
    reset();

    // Build a minimal Darwin-style initial stack:
    // argc = 0, argv[0] = NULL, envp[0] = NULL, apple[0] = NULL.
    // Keep all four words inside the mapped stack instead of placing SP
    // at the final word of memory, where startup code cannot walk the
    // argv/envp/Apple vectors safely.
    const std::uint64_t stackEnd = low + memory_.size();
    constexpr std::uint64_t INITIAL_STACK_WORDS = 4;
    constexpr std::uint64_t INITIAL_STACK_BYTES = INITIAL_STACK_WORDS * 4;
    if (memory_.size() < INITIAL_STACK_BYTES) {
        throw std::runtime_error("Mapped memory is too small for the initial process stack.");
    }
    const std::uint64_t stackPointer =
        (stackEnd - INITIAL_STACK_BYTES) & ~std::uint64_t{7};
    if (stackPointer < low ||
        stackPointer + INITIAL_STACK_BYTES > stackEnd) {
        throw std::runtime_error("Initial process stack does not fit in mapped memory.");
    }

    // The memory was zero-initialized; write the vector terminators
    // explicitly so the startup layout is documented and deterministic.
    for (std::uint64_t i = 0; i < INITIAL_STACK_WORDS; ++i) {
        write32(static_cast<std::uint32_t>(stackPointer + i * 4), 0);
    }

    regs_[13] = static_cast<std::uint32_t>(stackPointer);
    regs_[15] = image.entryPoint;
}

std::uint32_t Armv6::peek32(std::uint32_t address) const {
    return read32(address);
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

const std::vector<ArmTraceEntry>& Armv6::recentTrace() const {
    return trace_;
}

std::uint32_t Armv6::read32(std::uint32_t address) const {
    const std::uint64_t begin = memoryBase_;
    const std::uint64_t end = begin + memory_.size();
    const std::uint64_t offset = std::uint64_t(address) - begin;
    if (std::uint64_t(address) < begin || offset + 4 > memory_.size()) {
        std::ostringstream message;
        message << "32-bit memory read out of range. Address: 0x"
                << std::uppercase << std::hex << std::setw(8)
                << std::setfill('0') << address
                << ", mapped range: [0x" << std::setw(8) << begin
                << ", 0x" << std::setw(9) << end << ").";
        throw std::runtime_error(message.str());
    }
    return static_cast<std::uint32_t>(memory_[static_cast<std::size_t>(offset)]) |
           (static_cast<std::uint32_t>(memory_[static_cast<std::size_t>(offset + 1)]) << 8) |
           (static_cast<std::uint32_t>(memory_[static_cast<std::size_t>(offset + 2)]) << 16) |
           (static_cast<std::uint32_t>(memory_[static_cast<std::size_t>(offset + 3)]) << 24);
}

void Armv6::write16(std::uint32_t address, std::uint16_t value) {
    if (address < memoryBase_) throw std::runtime_error("16-bit memory write out of range.");
    const std::uint64_t offset = std::uint64_t(address) - memoryBase_;
    if (offset + 2 > memory_.size()) throw std::runtime_error("16-bit memory write out of range.");
    memory_[static_cast<std::size_t>(offset)] = static_cast<std::uint8_t>(value);
    memory_[static_cast<std::size_t>(offset + 1)] = static_cast<std::uint8_t>(value >> 8);
}

void Armv6::write32(std::uint32_t address, std::uint32_t value) {
    if (address < memoryBase_) throw std::runtime_error("32-bit memory write out of range.");
    const std::uint64_t offset = std::uint64_t(address) - memoryBase_;
    if (offset + 4 > memory_.size()) throw std::runtime_error("32-bit memory write out of range.");
    memory_[offset] = static_cast<std::uint8_t>(value);
    memory_[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    memory_[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    memory_[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::uint32_t Armv6::read16(std::uint32_t address) const {
    if (address < memoryBase_) throw std::runtime_error("16-bit memory read out of range.");
    const std::uint64_t offset = std::uint64_t(address) - memoryBase_;
    if (offset + 2 > memory_.size()) throw std::runtime_error("16-bit memory read out of range.");
    return static_cast<std::uint32_t>(memory_[static_cast<std::size_t>(offset)]) |
           (static_cast<std::uint32_t>(memory_[static_cast<std::size_t>(offset + 1)]) << 8);
}

std::uint32_t Armv6::read8(std::uint32_t address) const {
    if (address < memoryBase_) throw std::runtime_error("8-bit memory read out of range.");
    const std::uint64_t offset = std::uint64_t(address) - memoryBase_;
    if (offset >= memory_.size()) throw std::runtime_error("8-bit memory read out of range.");
    return memory_[static_cast<std::size_t>(offset)];
}

void Armv6::write8(std::uint32_t address, std::uint8_t value) {
    if (address < memoryBase_) throw std::runtime_error("8-bit memory write out of range.");
    const std::uint64_t offset = std::uint64_t(address) - memoryBase_;
    if (offset >= memory_.size()) throw std::runtime_error("8-bit memory write out of range.");
    memory_[static_cast<std::size_t>(offset)] = value;
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

    // BX (Branch and Exchange), register form.
    // The low bit selects ARM/Thumb state. This interpreter currently
    // supports ARM state only, so Thumb targets are normalized to ARM
    // word alignment; a Thumb decoder is still required for true interworking.
    if ((ins & 0x0FFFFFF0u) == 0x012FFF10u) {
        const unsigned rm = ins & 0xFu;
        const std::uint32_t target = rm == 15 ? address + 8 : regs_[rm];
        if (target & 1u) { cpsr_ |= FLAG_T; regs_[15] = target & ~1u; }
        else { cpsr_ &= ~FLAG_T; regs_[15] = target & ~3u; }
        return;
    }

    // BLX (Branch with Link and Exchange), register form (ARMv5+).
    if ((ins & 0x0FFFFFF0u) == 0x012FFF30u) {
        const unsigned rm = ins & 0xFu;
        if (rm == 15) {
            throw std::runtime_error("BLX with Rm == PC is unpredictable.");
        }

        const std::uint32_t target = regs_[rm];
        regs_[14] = address + 4;
        if (target & 1u) { cpsr_ |= FLAG_T; regs_[15] = target & ~1u; }
        else { cpsr_ &= ~FLAG_T; regs_[15] = target & ~3u; }
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

        std::uint32_t offset = ins & 0xFFFu;
        if (registerOffset) {
            // Register-offset addressing uses an immediate-shifted Rm.
            // Bit 4 must be zero for this ARM single-data-transfer encoding.
            if ((ins & (1u << 4)) != 0) {
                throw std::runtime_error("Unsupported register-shifted LDR/STR addressing.");
            }
            const unsigned rm = ins & 0xFu;
            const unsigned shiftType = (ins >> 5) & 3u;
            const unsigned shiftAmount = (ins >> 7) & 0x1Fu;
            const std::uint32_t value = rm == 15 ? address + 8 : regs_[rm];
            switch (shiftType) {
            case 0: // LSL #0 leaves the value unchanged.
                offset = shiftAmount == 0 ? value : value << shiftAmount;
                break;
            case 1: // LSR #0 encodes LSR #32.
                offset = shiftAmount == 0 ? 0u : value >> shiftAmount;
                break;
            case 2: // ASR #0 encodes ASR #32.
                offset = shiftAmount == 0
                    ? ((value & 0x80000000u) ? 0xFFFFFFFFu : 0u)
                    : static_cast<std::uint32_t>(static_cast<std::int32_t>(value) >> shiftAmount);
                break;
            case 3: // ROR #0 encodes RRX, using CPSR.C.
                if (shiftAmount == 0) {
                    offset = ((cpsr_ & FLAG_C) ? 0x80000000u : 0u) | (value >> 1);
                } else {
                    offset = (value >> shiftAmount) | (value << (32u - shiftAmount));
                }
                break;
            }
        }

        const std::uint32_t base = rn == 15 ? address + 8 : regs_[rn];
        const std::uint32_t adjusted = addOffset ? base + offset : base - offset;
        const std::uint32_t effective = preIndex ? adjusted : base;

        if (load) {
            const std::uint32_t value = byteTransfer ? read8(effective) : read32(effective);
            if (rd == 15) {
                const std::uint32_t target = value & ~3u;
                const std::uint64_t mappedBegin = memoryBase_;
                const std::uint64_t mappedEnd = mappedBegin + memory_.size();
                if (target < mappedBegin || std::uint64_t(target) + 4 > mappedEnd) {
                    std::ostringstream message;
                    message << "LDR loaded PC from an unmapped target. "
                            << "Error-causing addresses: "
                            << "instruction=0x" << std::uppercase << std::hex
                            << std::setw(8) << std::setfill('0') << address
                            << "; memory-read/pointer-slot=0x" << std::setw(8) << effective
                            << "; PC-base=0x" << std::setw(8) << base
                            << "; register-offset=0x" << std::setw(8) << offset
                            << "; loaded-branch-target=0x" << std::setw(8) << value
                            << "; aligned-target=0x" << std::setw(8) << target
                            << "; mapped-range=[0x" << std::setw(8) << mappedBegin
                            << ", 0x" << std::setw(9) << mappedEnd << ")."
                            << " Recent instruction addresses:"; 
                    for (const auto& item : trace_) {
                        message << " 0x" << std::setw(8) << item.pc;
                    }

                    bool foundPointerSection = false;
                    for (const auto& section : imageSections_) {
                        const std::uint64_t sectionEnd =
                            std::uint64_t(section.address) + section.size;
                        if (effective < section.address || effective + 4u > sectionEnd)
                            continue;
                        foundPointerSection = true;
                        const std::uint32_t sectionType = section.flags & 0xFFu;
                        message << " Section=" << section.segmentName << "."
                                << section.sectionName
                                << " (type=0x" << sectionType << ").";
                        if (sectionType == 0x6u || sectionType == 0x7u) {
                            const std::uint64_t slot = (effective - section.address) / 4u;
                            const std::uint64_t indirectIndex =
                                std::uint64_t(section.reserved1) + slot;
                            message << " Pointer slot index=" << std::dec << slot
                                    << ", indirect-symbol index=" << indirectIndex << ".";
                            if (indirectIndex < indirectSymbols_.size()) {
                                const std::uint32_t symbolIndex =
                                    indirectSymbols_[static_cast<std::size_t>(indirectIndex)];
                                message << " Indirect entry=0x" << std::hex << symbolIndex << ".";
                                if ((symbolIndex & 0xC0000000u) == 0) {
                                    if (symbolIndex < imageSymbols_.size()) {
                                        message << " Imported symbol="
                                                << imageSymbols_[symbolIndex].name << ".";
                                    } else {
                                        message << " Symbol index is outside the parsed symbol table.";
                                    }
                                } else {
                                    message << " Indirect entry is a LOCAL/ABS marker.";
                                }
                            } else {
                                message << " Indirect-symbol entry is unavailable.";
                            }
                        }
                        break;
                    }
                    if (!foundPointerSection)
                        message << " No Mach-O section contains the pointer slot.";
                    message << " The target is outside the loaded image; dependent dylibs and "
                            << "dyld binding are not implemented yet.";
                    throw std::runtime_error(message.str());
                }
                regs_[15] = target;
            } else {
                regs_[rd] = value;
            }
        } else {
            if (rd == 15) {
                throw std::runtime_error("STR with Rd == PC is unsupported in this interpreter.");
            }
            if (byteTransfer) write8(effective, static_cast<std::uint8_t>(regs_[rd]));
            else write32(effective, regs_[rd]);
        }

        if (!preIndex || writeBack) {
            if (rn == 15) {
                throw std::runtime_error("LDR/STR write-back with Rn == PC is unpredictable.");
            }
            regs_[rn] = adjusted;
        }
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
        bool shifterCarry = (cpsr_ & FLAG_C) != 0;

        if (immediate) {
            const unsigned rotate = ((ins >> 8) & 0xF) * 2;
            const std::uint32_t value = ins & 0xFFu;
            if (rotate == 0) {
                operand2 = value;
            } else {
                operand2 = (value >> rotate) | (value << (32 - rotate));
                shifterCarry = (operand2 >> 31) != 0;
            }
        } else {
            const unsigned rm = ins & 0xF;
            const std::uint32_t rmValue = rm == 15 ? address + 8 : regs_[rm];
            const unsigned shiftType = (ins >> 5) & 0x3;
            const bool registerShift = (ins & (1u << 4)) != 0;
            unsigned amount = 0;

            if (registerShift) {
                if ((ins & (1u << 7)) != 0) {
                    throw std::runtime_error("Invalid ARM register-shift encoding.");
                }
                const unsigned rs = (ins >> 8) & 0xF;
                amount = (rs == 15 ? address + 8 : regs_[rs]) & 0xFFu;
                if (amount == 0) {
                    operand2 = rmValue;
                } else {
                    switch (shiftType) {
                    case 0: // LSL
                        if (amount < 32) {
                            shifterCarry = ((rmValue >> (32 - amount)) & 1u) != 0;
                            operand2 = rmValue << amount;
                        } else if (amount == 32) {
                            shifterCarry = (rmValue & 1u) != 0;
                            operand2 = 0;
                        } else {
                            shifterCarry = false;
                            operand2 = 0;
                        }
                        break;
                    case 1: // LSR
                        if (amount < 32) {
                            shifterCarry = ((rmValue >> (amount - 1)) & 1u) != 0;
                            operand2 = rmValue >> amount;
                        } else if (amount == 32) {
                            shifterCarry = (rmValue >> 31) != 0;
                            operand2 = 0;
                        } else {
                            shifterCarry = false;
                            operand2 = 0;
                        }
                        break;
                    case 2: // ASR
                        if (amount < 32) {
                            shifterCarry = ((rmValue >> (amount - 1)) & 1u) != 0;
                            const std::int32_t signedValue = static_cast<std::int32_t>(rmValue);
                            operand2 = static_cast<std::uint32_t>(signedValue >> amount);
                        } else {
                            shifterCarry = (rmValue >> 31) != 0;
                            operand2 = shifterCarry ? 0xFFFFFFFFu : 0u;
                        }
                        break;
                    case 3: // ROR
                        amount &= 31u;
                        if (amount == 0) {
                            operand2 = rmValue;
                            shifterCarry = (rmValue >> 31) != 0;
                        } else {
                            operand2 = (rmValue >> amount) | (rmValue << (32 - amount));
                            shifterCarry = (operand2 >> 31) != 0;
                        }
                        break;
                    }
                }
            } else {
                amount = (ins >> 7) & 0x1Fu;
                switch (shiftType) {
                case 0: // LSL
                    if (amount != 0) {
                        shifterCarry = ((rmValue >> (32 - amount)) & 1u) != 0;
                        operand2 = rmValue << amount;
                    } else {
                        operand2 = rmValue;
                    }
                    break;
                case 1: // LSR; immediate zero means 32
                    if (amount == 0) {
                        shifterCarry = (rmValue >> 31) != 0;
                        operand2 = 0;
                    } else {
                        shifterCarry = ((rmValue >> (amount - 1)) & 1u) != 0;
                        operand2 = rmValue >> amount;
                    }
                    break;
                case 2: // ASR; immediate zero means 32
                    if (amount == 0) {
                        shifterCarry = (rmValue >> 31) != 0;
                        operand2 = shifterCarry ? 0xFFFFFFFFu : 0u;
                    } else {
                        shifterCarry = ((rmValue >> (amount - 1)) & 1u) != 0;
                        const std::int32_t signedValue = static_cast<std::int32_t>(rmValue);
                        operand2 = static_cast<std::uint32_t>(signedValue >> amount);
                    }
                    break;
                case 3: // ROR; immediate zero means RRX
                    if (amount == 0) {
                        operand2 = (static_cast<std::uint32_t>(shifterCarry) << 31) |
                                   (rmValue >> 1);
                        shifterCarry = (rmValue & 1u) != 0;
                    } else {
                        operand2 = (rmValue >> amount) | (rmValue << (32 - amount));
                        shifterCarry = (operand2 >> 31) != 0;
                    }
                    break;
                }
            }
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
            } else {
                cpsr_ &= ~FLAG_C;
                if (shifterCarry) cpsr_ |= FLAG_C;
            }
        }
        return;
    }

    throw std::runtime_error("Unsupported ARM instruction at address " +
                             std::to_string(address));
}

void Armv6::executeThumb(std::uint32_t instruction, std::uint32_t address) {
    const std::uint16_t ins = static_cast<std::uint16_t>(instruction);
    // Thumb-2 BLX (immediate): first halfword 11110..., second 11 J1 0 J2 imm10H 0.
    if ((ins & 0xF800u) == 0xF000u) {
        const std::uint16_t second = static_cast<std::uint16_t>(instruction >> 16);
        if ((second & 0xD001u) == 0xC000u) {
            const std::uint32_t sbit = (ins >> 10) & 1u;
            const std::uint32_t j1 = (second >> 13) & 1u;
            const std::uint32_t j2 = (second >> 11) & 1u;
            const std::uint32_t i1 = (~(j1 ^ sbit)) & 1u;
            const std::uint32_t i2 = (~(j2 ^ sbit)) & 1u;
            const std::uint32_t encoded = (sbit << 24) | (i1 << 23) | (i2 << 22) |
                ((ins & 0x03FFu) << 12) | (((second >> 1) & 0x03FFu) << 2);
            const std::int32_t offset = (encoded & 0x01000000u)
                ? static_cast<std::int32_t>(encoded | 0xFE000000u)
                : static_cast<std::int32_t>(encoded);
            regs_[14] = (address + 4) | 1u;
            const std::uint32_t base = (address + 4) & ~3u;
            regs_[15] = static_cast<std::uint32_t>(static_cast<std::int64_t>(base) + offset) & ~3u;
            cpsr_ &= ~FLAG_T; // BLX immediate enters ARM state.
            return;
        }
        throw std::runtime_error("Unsupported 32-bit Thumb instruction at address " + std::to_string(address));
    }
    const auto setNz = [&](std::uint32_t value) {
        cpsr_ &= ~(FLAG_N | FLAG_Z);
        if (value & 0x80000000u) cpsr_ |= FLAG_N;
        if (value == 0) cpsr_ |= FLAG_Z;
    };
    const auto setAdd = [&](std::uint32_t a, std::uint32_t b, std::uint32_t value) {
        setNz(value); cpsr_ &= ~(FLAG_C | FLAG_V);
        if (std::uint64_t(a) + b > 0xFFFFFFFFull) cpsr_ |= FLAG_C;
        if ((~(a ^ b) & (a ^ value)) & 0x80000000u) cpsr_ |= FLAG_V;
    };
    const auto setSub = [&](std::uint32_t a, std::uint32_t b, std::uint32_t value) {
        setNz(value); cpsr_ &= ~(FLAG_C | FLAG_V);
        if (a >= b) cpsr_ |= FLAG_C;
        if (((a ^ b) & (a ^ value)) & 0x80000000u) cpsr_ |= FLAG_V;
    };
    if ((ins & 0xE000u) == 0x0000u) {
        if ((ins & 0x1800u) == 0x1800u) {
            const bool imm = (ins & 0x0400u) != 0, sub = (ins & 0x0200u) != 0;
            const unsigned rn = (ins >> 3) & 7u, rd = ins & 7u;
            const std::uint32_t a = regs_[rn], b = imm ? ((ins >> 6) & 7u) : regs_[(ins >> 6) & 7u];
            const std::uint32_t v = sub ? a - b : a + b;
            regs_[rd] = v; if (sub) setSub(a,b,v); else setAdd(a,b,v); return;
        }
        const unsigned type = (ins >> 11) & 3u, amount = (ins >> 6) & 31u;
        const unsigned rm = (ins >> 3) & 7u, rd = ins & 7u;
        const std::uint32_t value = regs_[rm]; std::uint32_t result = value;
        bool carry = (cpsr_ & FLAG_C) != 0;
        if (type == 0) { if (amount) { carry = ((value >> (32-amount)) & 1u) != 0; result = value << amount; } }
        else if (type == 1) { const unsigned n=amount?amount:32; carry=((value>>(n-1))&1u)!=0; result=n==32?0:value>>n; }
        else if (type == 2) { const unsigned n=amount?amount:32; carry=((value>>(n-1))&1u)!=0; result=static_cast<std::uint32_t>(static_cast<std::int32_t>(value)>>n); }
        else throw std::runtime_error("Unsupported Thumb shift encoding.");
        regs_[rd]=result; setNz(result); cpsr_=(cpsr_&~FLAG_C)|(carry?FLAG_C:0); return;
    }
    if ((ins & 0xE000u) == 0x2000u) {
        const unsigned op=(ins>>11)&3u, rd=(ins>>8)&7u; const std::uint32_t imm=ins&255u, a=regs_[rd];
        const std::uint32_t v=op==0?imm:(op==1||op==3)?a-imm:a+imm;
        if(op!=1) regs_[rd]=v; if(op==1||op==3)setSub(a,imm,v); else if(op==2)setAdd(a,imm,v); else setNz(v); return;
    }
    if ((ins & 0xF800u) == 0x4800u) {
        const unsigned rd=(ins>>8)&7u; regs_[rd]=read32(((address+4)&~3u)+((ins&255u)<<2)); return;
    }
    if ((ins & 0xF000u) == 0xA000u) {
        const unsigned rd=(ins>>8)&7u; const std::uint32_t base=(ins&0x0800u)?regs_[13]:((address+4)&~3u);
        regs_[rd]=base+((ins&255u)<<2); return;
    }
    if ((ins & 0xF600u) == 0xB400u) {
        const bool pop=(ins&0x0800u)!=0, extra=(ins&0x0100u)!=0; std::uint32_t sp=regs_[13];
        if(!pop) { for(int r=7;r>=0;--r) if(ins&(1u<<r)){sp-=4;write32(sp,regs_[r]);} if(extra){sp-=4;write32(sp,regs_[14]);} }
        else { for(unsigned r=0;r<8;++r) if(ins&(1u<<r)){regs_[r]=read32(sp);sp+=4;} if(extra){const auto target=read32(sp);sp+=4;if(target&1u){cpsr_|=FLAG_T;regs_[15]=target&~1u;}else{cpsr_&=~FLAG_T;regs_[15]=target&~3u;}} }
        regs_[13]=sp; return;
    }
    if ((ins & 0xFF00u) == 0xB000u) { const std::uint32_t n=(ins&0x7Fu)<<2; regs_[13]=(ins&0x80u)?regs_[13]-n:regs_[13]+n; return; }
    if ((ins & 0xE000u) == 0x6000u) {
        const bool byte=(ins&0x1000u)!=0, load=(ins&0x0800u)!=0; const unsigned imm=(ins>>6)&31u, rb=(ins>>3)&7u, rd=ins&7u;
        const std::uint32_t ea=regs_[rb]+(byte?imm:imm*4u);
        if(load) regs_[rd]=byte?read8(ea):read32(ea); else if(byte)write8(ea,static_cast<std::uint8_t>(regs_[rd])); else write32(ea,regs_[rd]); return;
    }
    if ((ins & 0xF000u) == 0x9000u) {
        const bool load=(ins&0x0800u)!=0; const unsigned rd=(ins>>8)&7u; const std::uint32_t ea=regs_[13]+((ins&255u)<<2);
        if(load)regs_[rd]=read32(ea);else write32(ea,regs_[rd]);return;
    }
    if ((ins & 0xFC00u) == 0x4400u) {
        const unsigned op=(ins>>8)&3u, rd=(ins&7u)|((ins>>4)&8u), rs=(ins>>3)&15u;
        const std::uint32_t a=rd==15?address+4:regs_[rd], b=rs==15?address+4:regs_[rs];
        if(op==3){if(ins&0x80u)regs_[14]=(address+2)|1u;if(b&1u){cpsr_|=FLAG_T;regs_[15]=b&~1u;}else{cpsr_&=~FLAG_T;regs_[15]=b&~3u;}}
        else if(op==0){if(rd==15)regs_[15]=a+b;else regs_[rd]=a+b;}
        else if(op==1)setSub(a,b,a-b);else{if(rd==15)regs_[15]=b&~1u;else regs_[rd]=b;} return;
    }
    if ((ins & 0xF000u) == 0xD000u) {
        const unsigned cond=(ins>>8)&15u; if(cond>=14)throw std::runtime_error("Unsupported Thumb conditional branch.");
        if(conditionPassed(cond)){const std::int32_t off=static_cast<std::int8_t>(ins&255u)*2;regs_[15]=static_cast<std::uint32_t>(static_cast<std::int64_t>(address+4)+off);}return;
    }
    if ((ins & 0xF800u) == 0xE000u) {
        std::int32_t off=ins&0x7FFu;if(off&0x400)off|=~0x7FF;regs_[15]=static_cast<std::uint32_t>(static_cast<std::int64_t>(address+4)+off*2);return;
    }
    throw std::runtime_error("Unsupported Thumb instruction at address "+std::to_string(address));
}

void Armv6::step() {
    if (halted_) return;
    const std::uint32_t address = regs_[15];
    const bool thumb = (cpsr_ & FLAG_T) != 0;
    std::uint32_t instruction = thumb ? read16(address) : read32(address);
    const bool thumbWide = thumb && ((instruction & 0xF800u) == 0xF000u);
    if (thumbWide) instruction |= read16(address + 2) << 16;
    ArmTraceEntry entry;
    entry.pc = address; entry.instruction = instruction; entry.before = regs_;
    trace_.push_back(entry);
    constexpr std::size_t TRACE_LIMIT = 12;
    if (trace_.size() > TRACE_LIMIT) trace_.erase(trace_.begin());
    regs_[15] = address + (thumb ? (thumbWide ? 4u : 2u) : 4u);
    try {
        if (thumb) executeThumb(instruction, address);
        else execute(instruction, address);
        trace_.back().after = regs_; trace_.back().completed = true;
    } catch (...) {
        trace_.back().after = regs_; trace_.back().completed = false; throw;
    }
}

void Armv6::run(std::size_t instructionLimit) {
    std::size_t executed = 0;
    while (!halted_ && executed < instructionLimit) {
        step();
        ++executed;
    }
    if (executed == instructionLimit && !halted_) {
        std::cout << "Instruction limit reached; stopping safely.\n";
    }
}
