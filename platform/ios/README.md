# iOS host app

This folder contains the native iOS host app for iOS 15 and later.

## Build

Install XcodeGen, then run:

    cd platform/ios
    xcodegen generate
    xcodebuild -project OldiOSHost.xcodeproj -scheme OldiOSHost -configuration Release -sdk iphoneos -derivedDataPath build CODE_SIGNING_ALLOWED=NO

The GitHub Actions workflow packages the unsigned app bundle into an IPA artifact.

## C++ core and IPA inspection

The iOS target compiles and links the shared C++ sources:

- `src/armv6.cpp` — ARM interpreter prototype
- `src/ipa_loader.cpp` — ZIP/IPA reader, executable extraction, CRC validation and basic 32-bit ARM Mach-O header validation
- `src/macho_loader.cpp` — thin 32-bit ARM Mach-O load-command, segment, dependency and symbol metadata parser
- `third_party/miniz*.c` — ZIP deflate and CRC support

The SwiftUI host provides a document picker for IPA files. The Objective-C++ bridge passes the selected file to the existing IPA loader, extracts the app's main executable into the app's temporary directory, then parses its Mach-O metadata. The source IPA is opened read-only and is not modified.

The result screen reports the app bundle path, executable name and size, CPU type/subtype, segment count, linked libraries and entry point when available.

## Current limitations

- IPA support is limited to non-encrypted, non-ZIP64 archives using stored or deflate entries; fat/universal Mach-O selection is not implemented.
- The Mach-O loader maps one executable image but does not load dependent dylibs or apply dyld relocations/bindings.
- ARM/Thumb instruction support is incomplete.
- Objective-C runtime, Foundation/UIKit compatibility, NIB loading, graphics, touch, and legacy app execution are not implemented.
- Selecting and parsing an IPA is an inspection step only; it does not execute the old iOS application.
