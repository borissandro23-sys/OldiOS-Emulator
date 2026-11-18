# OldiOS for iOS

This is a companion iOS app for the OldiOS Emulator project.

## What it does

- Imports an IPA selected through the iOS Files picker.
- Extracts the `Payload/<App>.app` bundle into the app's private Documents directory.
- Reads the bundle display name and bundle identifier from Info.plist.
- Lists imported bundles and lets the user remove them.

## Important limitation

iOS does not let an ordinary App Store app install another app or execute its code from its sandbox. This app only imports IPA bundles into its own private library. It does not run ARMv6/ARMv7 apps and does not install them as system apps. Actual emulation is a separate engineering milestone.

## Build

The project is generated with XcodeGen and uses ZIPFoundation through Swift Package Manager.

```sh
brew install xcodegen
cd iOS
xcodegen generate
xcodebuild -project OldiOS.xcodeproj -scheme OldiOS -sdk iphoneos -configuration Release -derivedDataPath build CODE_SIGNING_ALLOWED=NO build
```

The GitHub Actions workflow packages the unsigned app bundle into an IPA artifact. Install it only through a method supported by your device and signing setup.
