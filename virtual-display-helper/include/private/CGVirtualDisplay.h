// SPDX-License-Identifier: Apache-2.0
#pragma once

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <dispatch/dispatch.h>

// Why: reverse-engineered from public macOS_headers (w0lfschild) — not stable across major macOS.
@interface CGVirtualDisplayDescriptor : NSObject
- (void)setName:(NSString *)name;
- (void)setMaxPixelsWide:(uint32_t)w;
- (void)setMaxPixelsHigh:(uint32_t)h;
- (void)setSizeInMillimeters:(CGSize)mm;
- (void)setProductID:(uint32_t)pid;
- (void)setVendorID:(uint32_t)vid;
- (void)setSerialNum:(uint32_t)sn;
- (dispatch_queue_t)queue;
@end

// Why: reverse-engineered private symbol — signature can change between macOS releases.
@interface CGVirtualDisplayMode : NSObject
- (instancetype)initWithWidth:(uint32_t)w height:(uint32_t)h refreshRate:(double)hz;
@end

// Why: reverse-engineered private symbol — apply via -[CGVirtualDisplay applySettings:].
@interface CGVirtualDisplaySettings : NSObject
- (void)setHiDPI:(uint32_t)hidpi;
- (void)setModes:(NSArray<CGVirtualDisplayMode *> *)modes;
@end

// Why: reverse-engineered — `displayID` returns a CGDirectDisplayID usable with public CG APIs.
@interface CGVirtualDisplay : NSObject
- (instancetype)initWithDescriptor:(CGVirtualDisplayDescriptor *)d;
- (BOOL)applySettings:(CGVirtualDisplaySettings *)s;
- (uint32_t)displayID;
- (dispatch_queue_t)dispatchQueue;
@end
