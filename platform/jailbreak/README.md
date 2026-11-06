# Jailbreak integration

This folder contains an initial rootless Theos tweak target for Dopamine-style jailbreak environments.

## Build

Install Theos and its iOS toolchain on macOS, then run:

```sh
make package
```

The resulting DEB is a **scaffold package only**. The current tweak constructor logs that it loaded. It does not intercept SpringBoard launches, identify legacy applications, or start the host app.

Launch interception must be implemented and tested against the supported iOS/SpringBoard versions before this package can be considered functional.
