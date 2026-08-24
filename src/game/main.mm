// Entry point. Everything from here down is Objective-C++ only because it
// touches AppKit for the run loop; the game itself (App::run) is portable
// C++ apart from the platform/renderer/audio backends it composes.
#include "game/App.hpp"
#import <Cocoa/Cocoa.h>

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv;
    @autoreleasepool {
        hv::game::App app;
        return app.run();
    }
}
