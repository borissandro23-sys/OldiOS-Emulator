# SadStore (iOS 15+)

A separate SwiftUI iOS app target, kept alongside the OldiOS-Emulator C++ project.

## Build

Requires macOS with Xcode 14 or newer and XcodeGen.

```sh
brew install xcodegen
cd SadStore
xcodegen generate
open SadStore.xcodeproj
```

Select the SadStore scheme and build for an iOS 15+ simulator or device.

## App icon

The AppIcon asset uses the original `fill-the-entire-background.jpg` artwork from the repository. The artwork is not replaced. Xcode expects an App Store icon image sized 1024×1024 pixels; if the source JPG has different dimensions, make a square 1024×1024 copy while preserving the original artwork, then keep the filename `AppIcon.jpg`.

The app is a starter UI only; no store/catalog backend is connected yet.
