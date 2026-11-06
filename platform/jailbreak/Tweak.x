#import <Foundation/Foundation.h>

// Scaffold only: this constructor confirms that the tweak loads.
// It does not hook SpringBoard, intercept app launches, or start the emulator.
%ctor {
    @autoreleasepool {
        NSLog(@"[OldiOSLauncher] scaffold loaded; launch interception is not implemented");
    }
}
