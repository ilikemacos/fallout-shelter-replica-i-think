// The one platform window implementation Haven ships: a Cocoa NSWindow with
// an NSOpenGLView core-profile 4.1 context. Vulkan/MoltenVK, when built,
// renders into a CAMetalLayer-backed sibling view instead (see VulkanSurface.mm);
// this file only ever creates the OpenGL path plus the shared window chrome.
#include "platform/Window.hpp"
#include "core/Log.hpp"
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

using hv::input::InputState;
using hv::input::Key;
using hv::input::MouseButton;

namespace {

hv::input::Key keyFromCode(unsigned short code) {
    using hv::input::Key;
    static const Key table[128] = {
        /*0x00*/ Key::A, Key::S, Key::D, Key::F, Key::H, Key::G, Key::Z, Key::X,
        /*0x08*/ Key::C, Key::V, Key::Unknown, Key::B, Key::Q, Key::W, Key::E, Key::R,
        /*0x10*/ Key::Y, Key::T, Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num6, Key::Num5,
        /*0x18*/ Key::Plus, Key::Num9, Key::Num7, Key::Minus, Key::Num8, Key::Num0, Key::Unknown, Key::O,
        /*0x20*/ Key::U, Key::Unknown, Key::I, Key::P, Key::Return, Key::L, Key::J, Key::Unknown,
        /*0x28*/ Key::K, Key::Unknown, Key::Unknown, Key::Unknown, Key::N, Key::M, Key::Unknown, Key::Tab,
        /*0x30*/ Key::Tab, Key::Space, Key::Unknown, Key::Backspace, Key::Unknown, Key::Escape, Key::Unknown, Key::LeftCmd,
        /*0x38*/ Key::LeftShift, Key::Unknown, Key::LeftOption, Key::LeftCtrl, Key::RightShift, Key::RightOption, Key::RightCtrl, Key::Unknown,
        /*0x40*/ Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown,
        /*0x48*/ Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown,
        /*0x50*/ Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown,
        /*0x58*/ Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown,
        /*0x60*/ Key::F5, Key::F6, Key::F7, Key::F3, Key::F8, Key::F9, Key::Unknown, Key::F11,
        /*0x68*/ Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::F10, Key::Unknown, Key::F12,
        /*0x70*/ Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown, Key::Unknown,
        /*0x78*/ Key::F2, Key::Unknown, Key::F1, Key::Left, Key::Right, Key::Down, Key::Up, Key::Unknown,
    };
    return code < 128 ? table[code] : Key::Unknown;
}

} // namespace

@interface HavenOpenGLView : NSOpenGLView {
@public
    InputState* input;
    bool focused;
}
@end

@implementation HavenOpenGLView

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)wantsUpdateLayer { return NO; }

- (void)keyDown:(NSEvent*)event {
    if (event.isARepeat) return;
    const Key k = keyFromCode(event.keyCode);
    if (k != Key::Unknown) {
        input->keyDown[static_cast<size_t>(k)] = true;
        input->keyPressed[static_cast<size_t>(k)] = true;
    }
    NSString* chars = event.characters;
    if (chars.length > 0) input->textInput += std::string([chars UTF8String]);
}
- (void)keyUp:(NSEvent*)event {
    const Key k = keyFromCode(event.keyCode);
    if (k != Key::Unknown) input->keyDown[static_cast<size_t>(k)] = false;
}
- (void)flagsChanged:(NSEvent*)event {
    const NSEventModifierFlags f = event.modifierFlags;
    input->keyDown[static_cast<size_t>(Key::LeftShift)] = (f & NSEventModifierFlagShift) != 0;
    input->keyDown[static_cast<size_t>(Key::LeftCmd)] = (f & NSEventModifierFlagCommand) != 0;
    input->keyDown[static_cast<size_t>(Key::LeftOption)] = (f & NSEventModifierFlagOption) != 0;
    input->keyDown[static_cast<size_t>(Key::LeftCtrl)] = (f & NSEventModifierFlagControl) != 0;
}

- (void)updateMousePos:(NSEvent*)event {
    const NSRect bounds = self.bounds;
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    const hv::Vec2 newPos{static_cast<float>(p.x), static_cast<float>(bounds.size.height - p.y)};
    input->mouseDelta.x += newPos.x - input->mousePos.x;
    input->mouseDelta.y += newPos.y - input->mousePos.y;
    input->mousePos = newPos;
}
- (void)mouseMoved:(NSEvent*)event { [self updateMousePos:event]; }
- (void)mouseDragged:(NSEvent*)event { [self updateMousePos:event]; }
- (void)rightMouseDragged:(NSEvent*)event { [self updateMousePos:event]; }
- (void)otherMouseDragged:(NSEvent*)event { [self updateMousePos:event]; }

- (void)mouseDown:(NSEvent*)event {
    input->mouseDown[static_cast<size_t>(MouseButton::Left)] = true;
    input->mousePressed[static_cast<size_t>(MouseButton::Left)] = true;
    [self updateMousePos:event];
}
- (void)mouseUp:(NSEvent*)event {
    input->mouseDown[static_cast<size_t>(MouseButton::Left)] = false;
    input->mouseReleased[static_cast<size_t>(MouseButton::Left)] = true;
}
- (void)rightMouseDown:(NSEvent*)event {
    input->mouseDown[static_cast<size_t>(MouseButton::Right)] = true;
    input->mousePressed[static_cast<size_t>(MouseButton::Right)] = true;
}
- (void)rightMouseUp:(NSEvent*)event {
    input->mouseDown[static_cast<size_t>(MouseButton::Right)] = false;
    input->mouseReleased[static_cast<size_t>(MouseButton::Right)] = true;
}
- (void)scrollWheel:(NSEvent*)event {
    input->scrollDelta += static_cast<float>(event.scrollingDeltaY) * (event.hasPreciseScrollingDeltas ? 0.02f : 0.5f);
}
- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self.window invalidateCursorRectsForView:self];
}

@end

namespace hv::platform {
namespace {

class CocoaWindow final : public Window {
public:
    CocoaWindow(const std::string& title, const hv::GraphicsSettings& gfx) {
        @autoreleasepool {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

            const NSRect frame = NSMakeRect(0, 0, gfx.windowWidth, gfx.windowHeight);
            NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                     NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
            window_ = [[NSWindow alloc] initWithContentRect:frame styleMask:style
                                                     backing:NSBackingStoreBuffered defer:NO];
            [window_ setTitle:[NSString stringWithUTF8String:title.c_str()]];
            [window_ center];
            [window_ setReleasedWhenClosed:NO];

            NSOpenGLPixelFormatAttribute attrs[] = {
                NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
                NSOpenGLPFAColorSize, 32, NSOpenGLPFADepthSize, 24,
                NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, 0
            };
            NSOpenGLPixelFormat* pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
            view_ = [[HavenOpenGLView alloc] initWithFrame:frame pixelFormat:pf];
            view_->input = &input_;
            view_->focused = true;
            [view_ setWantsBestResolutionOpenGLSurface:gfx.highDPI];
            [window_ setContentView:view_];
            [window_ makeFirstResponder:view_];
            [window_ makeKeyAndOrderFront:nil];

            GLint swapInterval = gfx.vsync ? 1 : 0;
            [[view_ openGLContext] setValues:&swapInterval forParameter:NSOpenGLContextParameterSwapInterval];
            [[view_ openGLContext] makeCurrentContext];

            applyWindowMode(gfx.windowMode);

            closeObserver_ = [[NSNotificationCenter defaultCenter]
                addObserverForName:NSWindowWillCloseNotification object:window_ queue:nil
                usingBlock:^(NSNotification*) { shouldClose_ = true; }];
            [NSApp activateIgnoringOtherApps:YES];
        }
    }

    ~CocoaWindow() override {
        if (closeObserver_) [[NSNotificationCenter defaultCenter] removeObserver:closeObserver_];
    }

    void applyWindowMode(hv::WindowMode mode) {
        const bool wantsFullscreen = mode != hv::WindowMode::Windowed;
        const bool isFullscreen = ([window_ styleMask] & NSWindowStyleMaskFullScreen) != 0;
        if (wantsFullscreen != isFullscreen) [window_ toggleFullScreen:nil];
    }

    void pumpEvents() override {
        input_.mousePressed.fill(false);
        input_.mouseReleased.fill(false);
        input_.keyPressed.fill(false);
        input_.mouseDelta = {0, 0};
        input_.scrollDelta = 0.0f;
        input_.textInput.clear();

        @autoreleasepool {
            NSEvent* event;
            while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil
                                                   inMode:NSDefaultRunLoopMode dequeue:YES])) {
                [NSApp sendEvent:event];
            }
        }
        input_.windowFocused = [window_ isKeyWindow];
    }

    bool shouldClose() const override { return shouldClose_; }
    void requestClose() override { [window_ performClose:nil]; shouldClose_ = true; }

    const InputState& input() const override { return input_; }

    DisplayMetrics metrics() const override {
        DisplayMetrics m;
        const NSRect content = [view_ bounds];
        const NSRect backing = [view_ convertRectToBacking:content];
        m.pointWidth = static_cast<hv::i32>(content.size.width);
        m.pointHeight = static_cast<hv::i32>(content.size.height);
        m.pixelWidth = static_cast<hv::i32>(backing.size.width);
        m.pixelHeight = static_cast<hv::i32>(backing.size.height);
        m.backingScale = m.pointWidth > 0 ? static_cast<hv::f32>(m.pixelWidth) / static_cast<hv::f32>(m.pointWidth) : 1.0f;
        return m;
    }

    void setWindowMode(hv::WindowMode mode) override { applyWindowMode(mode); }
    void setVSync(bool enabled) override {
        GLint swapInterval = enabled ? 1 : 0;
        [[view_ openGLContext] setValues:&swapInterval forParameter:NSOpenGLContextParameterSwapInterval];
    }
    void setTitle(const std::string& title) override {
        [window_ setTitle:[NSString stringWithUTF8String:title.c_str()]];
    }
    void swapBuffers() override {
        @autoreleasepool { [[view_ openGLContext] flushBuffer]; }
    }
    void* nativeWindowHandle() const override { return (__bridge void*)window_; }
    void* nativeViewHandle() const override { return (__bridge void*)view_; }
    bool isFocused() const override { return input_.windowFocused; }

private:
    NSWindow* window_ = nil;
    HavenOpenGLView* view_ = nil;
    InputState input_;
    bool shouldClose_ = false;
    id closeObserver_ = nil;
};

} // namespace

std::unique_ptr<Window> Window::create(const std::string& title, const hv::GraphicsSettings& gfx) {
    return std::make_unique<CocoaWindow>(title, gfx);
}

} // namespace hv::platform
