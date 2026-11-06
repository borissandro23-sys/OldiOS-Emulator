# OldiOS-Emulator

An educational ARM instruction-emulation project. The first milestone is a small ARM-state interpreter that can be built on Windows, Linux, and macOS with CMake and a C++17 compiler.

## Current status

This is an early CPU-core prototype, **not an iOS emulator** and not an IPA launcher.

Implemented in this first milestone:

- 16 ARM registers (r0-r15) and a basic CPSR flag model
- ARM condition checks
- A subset of ARM data-processing instructions
- Immediate-offset word/byte LDR and STR
- B and BL branches
- A small built-in machine-code demonstration

Not implemented:

- Thumb instruction set
- Full ARMv6 instruction coverage, exceptions, coprocessors, MMU, or accurate timing
- Mach-O loading, iOS system frameworks, UIKit, graphics, audio, or touch input
- Running iOS applications or IPA files

A CPU instruction interpreter alone cannot run an iOS app. An actual app also depends on the executable format, operating-system services, frameworks, graphics, and other runtime components. This repository deliberately starts with a small, testable CPU core.

## Build on Windows

Install:
- Git
- CMake
- Visual Studio 2022 with the **Desktop development with C++** workload, or another C++17 compiler

Then, from the repository folder:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\oldios-emu.exe
```

If using MinGW or another single-configuration generator, the executable may be at `build\oldios-emu.exe`.

## Expected demo

The demo executes a short ARM instruction sequence, stores a value in emulated memory, loads it back, and checks the zero flag. The expected final values include:

- `r3 = 10`
- CPSR zero flag set

## Project direction

Possible future milestones:

1. Add unit tests for each implemented instruction.
2. Add Thumb decoding and execution.
3. Add a small assembler or load raw ARM test binaries.
4. Improve memory access and exceptions.
5. Research what additional operating-system compatibility work would be required for historical software preservation.

Only use software and firmware images that you are legally entitled to use.
