// macOS Dock icon. The app ships as a plain binary, not an .app bundle, so it
// has no .icns; the icon is drawn at startup (ui::DrawAppIconRGBA) and handed
// to NSApp. Call after glfwInit(), which creates NSApp.
#import <Cocoa/Cocoa.h>
#include <cstring>
#include "ui/AppIcon.h"

void SetMacDockIcon() {
    @autoreleasepool {
        constexpr int n = 512;
        const std::vector<uint8_t> px = ui::DrawAppIconRGBA(n);
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nullptr
                          pixelsWide:n
                          pixelsHigh:n
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                        bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
                         bytesPerRow:n * 4
                        bitsPerPixel:32];
        if (!rep) return;
        std::memcpy([rep bitmapData], px.data(), px.size());
        NSImage* img = [[NSImage alloc] initWithSize:NSMakeSize(n, n)];
        [img addRepresentation:rep];
        [NSApp setApplicationIconImage:img];
        [img release];
        [rep release];
    }
}
