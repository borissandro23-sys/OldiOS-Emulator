# OldiOS-Emulator

An educational iOS-era app bundle installer, executable loader, and ARM emulation prototype. It can install a 32-bit ARM app bundle from an IPA into a local `Applications` folder and inspect its Mach-O executable. It is **not yet capable of running iOS applications**.

## Current status

Implemented:
- Basic ARM-state interpreter prototype (limited instruction subset)
- Win32 iPhone-like 320x480 shell
- IPA ZIP central-directory parsing
- ZIP stored and deflate decompression (vendored miniz 2.1.0)
- Safe extraction of the complete `Payload/<App>.app` bundle into `./Applications` (Ctrl+I)
- Basic home-screen label for the installed bundle
- Removal of the app installed during the current emulator session (Ctrl+U)
- Safe extraction of the app's main executable into the system temporary directory
- ZIP CRC validation
- 32-bit little-/big-endian Mach-O header validation for ARM executables
- Checks that the Mach-O file type is MH_EXECUTE

Not implemented:
- Universal/fat Mach-O architecture selection
- Binary plist parsing (CFBundleExecutable falls back to the .app bundle name)
- Complete ARMv6/Thumb instruction support
- Mach-O load-command processing, dynamic linking, relocations, and dyld
- Objective-C runtime, Foundation, UIKit, graphics, audio, or touch event services
- Persistent installed-app catalog and full home-screen icon rendering
- Running an iOS application

**Important:** Loading a Mach-O file only means extracting it and validating its header. It does not execute the app. The emulator does not yet provide the iOS operating-system environment required by applications.

## Build on Windows

Install Git, CMake, and a C++17 compiler (Visual Studio 2022 or MinGW-w64).

For MinGW:
```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
./build/oldios-emu.exe
```

For Visual Studio:
```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\oldios-emu.exe
```

Controls:
- `Ctrl+I` — choose an IPA and install its complete `.app` bundle into `./Applications`.
- `Ctrl+U` — remove the app installed during the current emulator session.
- `Ctrl+O` — inspect the IPA executable and open the ARM debugger.

The home screen currently shows a generic placeholder tile and the installed bundle name; it does not yet read or display the app's real icon. Installing copies the app bundle but does not launch it. The current uninstaller tracks the app installed in the current session only.

## Third-party code

The ZIP deflate implementation is miniz 2.1.0, distributed under its permissive license included in `third_party/miniz.h` and `third_party/miniz.c`.

## Roadmap

1. Add tests using real IPA files and inspect their Mach-O load commands.
2. Add thin/fat Mach-O parsing and segment loading.
3. Expand ARMv6 and Thumb execution with unit tests.
4. Implement a small Mach-O runtime and required system-call stubs.
5. Research a minimal Objective-C/Foundation/UIKit compatibility layer.

Only use software and firmware images you are legally entitled to use.
