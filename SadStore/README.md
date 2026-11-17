# SadStore (iOS 15+)

A separate SwiftUI iOS app target, kept alongside the OldiOS-Emulator C++ project.

## Prepare the original app icon

Run this from the repository root on macOS:

```sh
mkdir -p SadStore/SadStore/Assets.xcassets/AppIcon.appiconset
sips -s format jpeg -Z 1024 fill-the-entire-background.jpg --out SadStore/SadStore/Assets.xcassets/AppIcon.appiconset/AppIcon.jpg
```

This keeps the repository's original JPG unchanged and creates a 1024px-high app-icon copy. The source image should be square for the icon to have the intended proportions.

## Build

Requires macOS with Xcode 14 or newer and XcodeGen.

```sh
brew install xcodegen
cd SadStore
xcodegen generate
open SadStore.xcodeproj
```

Select the SadStore scheme and build for an iOS 15+ simulator or device.

The app is a starter UI only; no store/catalog backend is connected yet.
