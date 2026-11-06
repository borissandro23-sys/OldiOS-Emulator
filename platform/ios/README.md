# iOS host app

This folder contains the first native iOS host-app target. It is designed to run on iOS 15 and later and will eventually host the shared ARMv6/Mach-O compatibility core.

## Build

The Xcode project is generated from `project.yml` with XcodeGen:

```sh
brew install xcodegen
cd platform/ios
xcodegen generate
xcodebuild -project OldiOSHost.xcodeproj -scheme OldiOSHost -configuration Release -sdk iphoneos -derivedDataPath build CODE_SIGNING_ALLOWED=NO
```

The GitHub Actions workflow packages the unsigned app bundle into an IPA artifact. Installation and launch still depend on a compatible signing/installation method and device policy.

## Current limitations

- This is only a native SwiftUI host shell.
- The C++ ARMv6/Mach-O core is not linked into this target yet.
- There is no dyld, Objective-C runtime, Foundation/UIKit compatibility implementation, NIB loader, or legacy-app execution.
- An IPA produced at this stage is a test shell, not an emulator capable of running I Am Rich.
