// SPDX-License-Identifier: Apache-2.0
#include "fuvr/iosurface_bridge.hpp"

#import <Foundation/Foundation.h>
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>

namespace fuvr::daemon {

CVPixelBufferRef pixelBufferFromMachPort(mach_port_t port) {
    if (port == MACH_PORT_NULL) return nullptr;

    IOSurfaceRef surface = IOSurfaceLookupFromMachPort(port);
    if (!surface) return nullptr;

    NSDictionary *attrs = @{
        (id)kCVPixelBufferIOSurfacePropertiesKey : @{},
    };
    CVPixelBufferRef pb = nullptr;
    CVReturn rv = CVPixelBufferCreateWithIOSurface(
        kCFAllocatorDefault, surface, (__bridge CFDictionaryRef)attrs, &pb);
    CFRelease(surface);
    if (rv != kCVReturnSuccess) return nullptr;
    return pb;
}

} // namespace fuvr::daemon
