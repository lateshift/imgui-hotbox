#include "system_appearance.hpp"

#import <AppKit/AppKit.h>

namespace {

char appearance_context;

} // namespace

@interface HotboxAppearanceObserver : NSObject {
    NSApplication* application_;
    BOOL dark_;
}
- (instancetype)initWithApplication:(NSApplication*)application;
- (BOOL)isDark;
@end

@implementation HotboxAppearanceObserver
- (instancetype)initWithApplication:(NSApplication*)application {
    self = [super init];
    if (self) {
        application_ = [application retain];
        [application_ addObserver:self forKeyPath:@"effectiveAppearance"
            options:NSKeyValueObservingOptionInitial context:&appearance_context];
    }
    return self;
}

- (void)dealloc {
    [application_ removeObserver:self forKeyPath:@"effectiveAppearance" context:&appearance_context];
    [application_ release];
    [super dealloc];
}

- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object
        change:(NSDictionary<NSKeyValueChangeKey, id>*)change context:(void*)context {
    if (context != &appearance_context) {
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
        return;
    }
    const auto appearance = [application_.effectiveAppearance bestMatchFromAppearancesWithNames:
        @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
    dark_ = [appearance isEqualToString:NSAppearanceNameDarkAqua];
}

- (BOOL)isDark { return dark_; }
@end

namespace hotbox::detail {

ThemeMode SystemAppearance() {
    if (!NSApp)
        return ThemeMode::Light;
    @autoreleasepool {
        static const struct Observer {
            HotboxAppearanceObserver* value = [[HotboxAppearanceObserver alloc] initWithApplication:NSApp];
            ~Observer() { [value release]; }
        } observer;
        return observer.value.isDark ? ThemeMode::Dark : ThemeMode::Light;
    }
}

} // namespace hotbox::detail
