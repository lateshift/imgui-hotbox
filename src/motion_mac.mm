#import <AppKit/AppKit.h>

namespace hotbox::detail {

bool SystemReduceMotion() {
    return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
}

} // namespace hotbox::detail
