# iOS port architecture

The project is being split into three layers:

1. **Shared compatibility core** — portable C/C++ code for ARMv6/Thumb execution, 32-bit Mach-O mapping, relocations, symbol resolution, and compatibility services.
2. **Native iOS host app** — a foreground SwiftUI app that will own the display and input lifecycle and link the shared core.
3. **Jailbreak integration** — a rootless Theos tweak that will detect launches of supported legacy app bundles and hand off to the host app.

## Current milestone

The host app and Theos package are build scaffolds only. The existing interpreter and Mach-O loader are not yet integrated into the iOS app. The tweak does not intercept launches. Neither target can currently run an original iOS application.

## Required engineering work

- Extract the portable interpreter and loader from the current desktop executable into a shared library target.
- Add unit tests for ARMv6/Thumb instructions, Mach-O segments, relocations, and symbol binding.
- Implement a bounded compatibility layer for the target app's required Objective-C runtime and system APIs.
- Load and resolve dependent system libraries safely; do not branch to unresolved dyld binder addresses.
- Implement NIB/resource loading and a UIKit-compatible view/event layer.
- Add explicit app allowlisting and a reliable host-app handoff for the jailbreak integration.
- Test the standalone IPA and rootless DEB independently on the target iOS versions.

## Target

Initial test device: iPhone 6s, iOS 15.8.8, Dopamine. The device runs 64-bit iOS; the legacy ARMv6 executable must be interpreted by the compatibility core rather than loaded natively.
