#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "macho_loader.hpp"

struct ArmTraceEntry {
    std::uint32_t pc = 0;
    std::uint32_t instruction = 0;
    std::array<std::uint32_t, 16> before{};
    std::array<std::uint32_t, 16> after{};
    bool completed = false;
};

class Armv6 {
public:
    static constexpr std::size_t kMemorySize = 64 * 1024;

    Armv6();

    void reset();
    void loadProgram(const std::vector<std::uint32_t>& words,
                     std::uint32_t address = 0);
    void loadImage(const MachOImage& image);
    std::uint32_t peek32(std::uint32_t address) const;
    void step();
    void run(std::size_t instructionLimit);

    std::uint32_t reg(std::size_t index) const;
    std::uint32_t cpsr() const;
    bool halted() const;
    const std::vector<ArmTraceEntry>& recentTrace() const;

private:
    std::array<std::uint32_t, 16> regs_{};
    std::uint32_t cpsr_ = 0;
    std::vector<std::uint8_t> memory_;
    std::uint32_t memoryBase_ = 0;
    bool halted_ = false;
    std::vector<ArmTraceEntry> trace_;

    std::uint32_t read16(std::uint32_t address) const;
    void write16(std::uint32_t address, std::uint16_t value);
    std::uint32_t read32(std::uint32_t address) const;
    void write32(std::uint32_t address, std::uint32_t value);
    std::uint32_t read8(std::uint32_t address) const;
    void write8(std::uint32_t address, std::uint8_t value);

    bool conditionPassed(std::uint32_t condition) const;
    void setNz(std::uint32_t value);
    void execute(std::uint32_t instruction, std::uint32_t instructionAddress);
    void executeThumb(std::uint16_t instruction, std::uint32_t instructionAddress);
};
