# iOS host app

This folder contains the native iOS host app for iOS 15 and later.

## Build

Install XcodeGen, then run:

    cd platform/ios
    xcodegen generate
    xcodebuild -project OldiOSHost.xcodeproj -scheme OldiOSHost -configuration Release -sdk iphoneos -derivedDataPath build CODE_SIGNING_ALLOWED=NO

The GitHub Actions workflow packages the unsigned app bundle into an IPA artifact.

## C++ core integration

The iOS target now compiles and links the shared C++ sources:

- src/armv6.cpp — ARM interpreter prototype
- src/macho_loader.cpp — thin 32-bit ARM Mach-O metadata/segment loader
- include/ — shared public headers

OldiOSCoreBridge.mm exposes a small Objective-C++ bridge to Swift. On launch, the host runs a tiny ARM instruction self-test (MOV R0, #42) and displays the result.

The desktop CMake target links the same oldios_core static library, keeping the CPU and Mach-O code separate from the Windows UI.

## Current limitations

- The bridge self-test confirms that the C++ core is compiled, linked, and callable from Swift; it does not run I Am Rich.
- The Mach-O loader parses and maps one executable image, but does not load dependent dylibs or apply dyld relocations/bindings.
- ARM/Thumb instruction support is incomplete.
- Objective-C runtime, Foundation/UIKit compatibility, NIB loading, graphics, touch, and legacy app execution are not implemented.
- The iOS target does not yet unpack IPA files; the current stage is core integration and a startup self-test.
