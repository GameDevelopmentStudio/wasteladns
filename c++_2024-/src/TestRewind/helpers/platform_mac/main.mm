#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif

@interface AppDelegate : NSObject<NSApplicationDelegate> { bool terminated; }
- (id)init;
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender;
- (bool)applicationHasTerminated;
@end
@implementation AppDelegate
- (id)init {
    if (!(self = [super init])) { return nil; }
    self->terminated = false;
    return self;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    self->terminated = true;
    return NSTerminateCancel;
}
- (bool)applicationHasTerminated { return self->terminated; }
@end

@interface MainWindowDelegate: NSObject<NSWindowDelegate>
@end
@implementation MainWindowDelegate
- (void)windowWillClose:(id)sender {
    [NSApp terminate:nil];
}
@end

int main(int , char** ) {
    @autoreleasepool {
        
        Platform::State platform = {};
        Allocator::init_arena(platform.memory.scratchArenaRoot, 1 << 20); // 1MB
        __DEBUGEXP(platform.memory.scratchArenaHighmark = (uintptr_t)platform.memory.scratchArenaRoot.curr; platform.memory.scratchArenaRoot.highmark = &platform.memory.scratchArenaHighmark);
        
        [NSApplication sharedApplication];
        AppDelegate* appDelegate = [[AppDelegate alloc] init];
        [NSApp setDelegate:appDelegate];
        
        // Need for menu bar and window focus
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        
        [NSApp setPresentationOptions:NSApplicationPresentationDefault];
        [NSApp activateIgnoringOtherApps:YES];
        
        // Press and Hold prevents some keys from emitting repeated characters
        NSDictionary* defaults = @{@"ApplePressAndHoldEnabled":@NO};
        [[NSUserDefaults standardUserDefaults] registerDefaults:defaults];
        
        id window;
        f32 windowScale = 1.f;
        NSOpenGLContext* openGLContext;
        {
            Platform::WindowConfig config;
            Game::loadLaunchConfig(config);
            NSString* nstitle = [NSString stringWithUTF8String:config.title];
            
            // Menu
            {
                id menuBar = [[NSMenu alloc] init];
                id appMenuItem = [[NSMenuItem alloc] init];
                [menuBar addItem:appMenuItem];
                id appMenu = [[NSMenu alloc] init];
                [NSApp setMainMenu:menuBar];
                id quitMenuItem = [[NSMenuItem alloc]
                                   initWithTitle:[@"Quit " stringByAppendingString:nstitle]
                                   action:@selector(terminate:) keyEquivalent:@"q"];
                [appMenu addItem:quitMenuItem];
                [appMenuItem setSubmenu: appMenu];
            }
            
            // Window
            window = [[NSWindow alloc]
                      initWithContentRect:NSMakeRect(0, 0, config.window_width, config.window_height) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
            [window setReleasedWhenClosed:NO]; // since the autorelease pool will release it too
            [window setTitle:nstitle];
            
            MainWindowDelegate* windowDelegate = [[MainWindowDelegate alloc] init];
            [window setDelegate:windowDelegate];
            
            NSView* contentView = [window contentView];
            
            NSOpenGLPixelFormatAttribute glAttributes[] = {
                NSOpenGLPFAAccelerated, NSOpenGLPFAClosestPolicy,
                NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
                NSOpenGLPFAColorSize, (NSOpenGLPixelFormatAttribute) 24,
                NSOpenGLPFAAlphaSize, (NSOpenGLPixelFormatAttribute) 8,
                NSOpenGLPFADepthSize, (NSOpenGLPixelFormatAttribute) 24,
                NSOpenGLPFAStencilSize, (NSOpenGLPixelFormatAttribute) 8,
                NSOpenGLPFADoubleBuffer,
                NSOpenGLPFASampleBuffers, (NSOpenGLPixelFormatAttribute) 0,
                0
            };
            NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:glAttributes];
            
            openGLContext = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];
            
            GLint swap = 1;
            [openGLContext setValues:&swap forParameter:NSOpenGLContextParameterSwapInterval];
            [openGLContext setView:contentView];
            [openGLContext makeCurrentContext];
            
            Renderer::Driver::loadGLExtensions();
            
            [(NSWindow*)window center];
            [window orderFrontRegardless];
            
            [NSApp finishLaunching];
            
            NSSize windowSize = [contentView frame].size;
            
            windowScale = [contentView convertSizeToBacking:CGSizeMake(1,1)].width;
            platform.screen.window_width = windowSize.width * windowScale;
            platform.screen.window_height = windowSize.height * windowScale;
            platform.screen.width = config.game_width;
            platform.screen.height = config.game_height;
            platform.screen.desiredRatio = platform.screen.width / (f32)platform.screen.height;
            platform.screen.fullscreen = config.fullscreen;
            __DEBUGEXP(platform.screen.text_scale = windowScale);
        }
        
        const int hotkeyMask = NSEventModifierFlagCommand | NSEventModifierFlagOption | NSEventModifierFlagControl | NSEventModifierFlagCapsLock;
        const f32 actualWindowHeight = [[window contentView] frame].size.height;
        {   // init mouse pos so we don't get a big delta movement on the first click
            NSPoint pos = [window  mouseLocationOutsideOfEventStream];
            platform.input.mouse.x = pos.x;
            platform.input.mouse.y = actualWindowHeight-pos.y;
        }
        // gamepad
        ::Input::Gamepad::init_hid_pads_mac(platform);
        
        mach_timebase_info_data_t ticks_to_nanos;
        mach_timebase_info(&ticks_to_nanos);
        f64 frequency = (1e9 * ticks_to_nanos.denom) / (f64)ticks_to_nanos.numer;
        
        platform.time.running = 0.0;
        platform.time.now = platform.time.start = mach_absolute_time() / frequency;
        
        Game::Instance game;
        Platform::GameConfig config;
        Game::start(game, config, platform);
        
        do
        {
            @autoreleasepool {
                if (platform.time.now >= config.nextFrame) {
                    // Input
                    {
                        for (u32 i = 0; i < platform.input.padCount; i++) { platform.input.pads[i].last_keys = platform.input.pads[i].curr_keys; }
                        memcpy(platform.input.keyboard.last, platform.input.keyboard.current, sizeof(u8)* ::Input::Keyboard::Keys::COUNT);
                        memcpy(platform.input.mouse.last, platform.input.mouse.curr, sizeof(u8) * ::Input::Mouse::Keys::COUNT);
                        const f32 mouse_prevx = platform.input.mouse.x, mouse_prevy = platform.input.mouse.y;
                        platform.input.mouse.dx = platform.input.mouse.dy = platform.input.mouse.scrolldx = platform.input.mouse.scrolldy = 0.f;
                        
                        NSEvent *event = nil;
                        do {
                            event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                       untilDate:[NSDate distantPast]
                                                          inMode:NSDefaultRunLoopMode
                                                         dequeue:YES];
                            NSEventType eventType = [event type];
                            bool event_passthrough = true;
                            switch (eventType) {
                                case NSEventTypeKeyUp: {
                                    if ([event modifierFlags] & hotkeyMask) { break; } // Handle events like cmd+q etc
                                    platform.input.keyboard.current[(::Input::Keyboard::Keys::Enum)[event keyCode]] = 0;
                                    event_passthrough = false; // disable key error sound
                                } break;
                                case NSEventTypeKeyDown: {
                                    if ([event modifierFlags] & hotkeyMask) { break; } // Handle events like cmd+q etc
                                    platform.input.keyboard.current[(::Input::Keyboard::Keys::Enum)[event keyCode]] = 1;
                                    event_passthrough = false; // disable key error sound
                                } break;
                                case NSEventTypeMouseMoved:
                                case NSEventTypeLeftMouseDragged:
                                case NSEventTypeRightMouseDragged:
                                case NSEventTypeOtherMouseDragged: {
                                    // todo: won't work when hiding cursor
                                    NSPoint pos = [event locationInWindow];
                                    platform.input.mouse.x = pos.x;
                                    platform.input.mouse.y = actualWindowHeight-pos.y;
                                    platform.input.mouse.dx = platform.input.mouse.x - mouse_prevx;
                                    platform.input.mouse.dy = platform.input.mouse.y - mouse_prevy;
                                } break;
                                case NSEventTypeLeftMouseDown:
                                case NSEventTypeRightMouseDown: {
                                    ::Input::Mouse::Keys::Enum keycode = (::Input::Mouse::Keys::Enum)((eventType >> 1) & 0x1);
                                    platform.input.mouse.curr[keycode] = 1;
                                } break;
                                case NSEventTypeLeftMouseUp:
                                case NSEventTypeRightMouseUp: {
                                    ::Input::Mouse::Keys::Enum keycode = (::Input::Mouse::Keys::Enum)((eventType >> 2) & 0x1);
                                    platform.input.mouse.curr[keycode] = 0;
                                } break;
                                    
                                case NSEventTypeScrollWheel: {
                                    float scrollx = [event deltaX];
                                    float scrolly = [event deltaY];
                                    platform.input.mouse.scrolldx += scrollx;
                                    platform.input.mouse.scrolldy += scrolly;
                                } break;
                                default: break;
                            }
                            if (event_passthrough) { // passthrough to handle things like title bar buttons
                                [NSApp sendEvent:event];
                            }
                        } while (event);
                        [NSApp updateWindows];
                        config.quit = [appDelegate applicationHasTerminated];
                    }
                    
                    [openGLContext makeCurrentContext];
                    Game::update(game, config, platform);
                    [openGLContext flushBuffer];
                }
                
                u64 now = mach_absolute_time();
                platform.time.now = now / frequency;
                platform.time.running = platform.time.now - platform.time.start;
            }
        } while (!config.quit);
        
    }
    return 1;
}