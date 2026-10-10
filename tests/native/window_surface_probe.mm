// SPDX-License-Identifier: MIT
// Real CAMetalLayer/EGL lifetime contract; not Android Activity acceptance.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include "artbox/native_surface.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static unsigned pixels_verified = 0, frames_presented = 0;
static void require(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "Window surface contract failed: %s (EGL %x, GL %x)\n",
                     message, eglGetError(), glGetError());
        std::exit(1);
    }
}

static void color(artbox_native_surface *surface, unsigned width, unsigned height,
                  unsigned char r, unsigned char g, unsigned char b) {
    require(artbox_native_surface_bind(surface) == ARTBOX_SURFACE_OK, "bind window context");
    glViewport(0, 0, width, height);
    glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    std::vector<unsigned char> pixels(width * height * 4, 0xcc);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    require(glGetError() == GL_NO_ERROR, "window readback");
    for (unsigned p = 0; p < width * height; ++p)
        require(pixels[p*4] == r && pixels[p*4+1] == g && pixels[p*4+2] == b &&
                pixels[p*4+3] == 255, "all window pixels have expected color");
    pixels_verified += width * height;
    require(artbox_native_surface_present(surface) == ARTBOX_SURFACE_OK, "present window");
    ++frames_presented;
    [CATransaction flush];
}

static NSWindow *window(CAMetalLayer **layer, CGFloat size) {
    NSWindow *result = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, size, size)
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    result.releasedWhenClosed = NO;
    result.contentView.wantsLayer = YES;
    *layer = [CAMetalLayer layer];
    result.contentView.layer = *layer;
    (*layer).contentsScale = 1;
    [result orderFront:nil];
    [CATransaction flush];
    return result;
}

int main() {
    @autoreleasepool {
        CFAbsoluteTime start = CFAbsoluteTimeGetCurrent();
        require([NSThread isMainThread], "main UI thread");
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        require(device != nil, "Metal device required for window acceptance");
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];
        CAMetalLayer *firstLayer, *secondLayer;
        NSWindow *firstWindow = window(&firstLayer, 32), *secondWindow = window(&secondLayer, 16);
        artbox_native_surface *first = nullptr, *second = nullptr;
        require(artbox_native_surface_create(nullptr, &first) == ARTBOX_SURFACE_ARGUMENT && !first,
                "reject missing layer without publishing ownership");
        require(artbox_native_surface_create((void *)firstLayer, nullptr) == ARTBOX_SURFACE_ARGUMENT,
                "reject missing output");
        NSObject *wrongLayer = [[NSObject alloc] init];
        require(artbox_native_surface_create((void *)wrongLayer, &first) == ARTBOX_SURFACE_ARGUMENT,
                "reject non-Metal layer");
        [wrongLayer release];
        firstLayer.bounds = CGRectZero;
        require(artbox_native_surface_create((void *)firstLayer, &first) == ARTBOX_SURFACE_ARGUMENT,
                "reject empty geometry");
        firstLayer.bounds = CGRectMake(0, 0, 32, 32);
        require(artbox_native_surface_create((void *)firstLayer, &first) == ARTBOX_SURFACE_OK && first,
                "create first window");
        artbox_native_surface *duplicate = nullptr;
        require(artbox_native_surface_create((void *)firstLayer, &duplicate) == ARTBOX_SURFACE_STATE && !duplicate,
                "one surface per layer");
        require(artbox_native_surface_create((void *)secondLayer, &first) == ARTBOX_SURFACE_ARGUMENT,
                "do not overwrite owned handle");
        require(artbox_native_surface_present(first) == ARTBOX_SURFACE_STATE, "present requires binding");
        color(first, 32, 32, 255, 0, 0);
        const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        require(renderer && std::strstr(renderer, "Metal"), "Metal renderer");
        NSString *rendererName = [NSString stringWithUTF8String:renderer];
        GLuint texture;
        glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
        const unsigned char texel[] = {19, 83, 227, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
        require(glGetError() == GL_NO_ERROR, "context resource creation");
        require(artbox_native_surface_create((void *)secondLayer, &second) == ARTBOX_SURFACE_OK,
                "create concurrent second window");
        require(glIsTexture(texture), "creation preserves first binding");
        color(second, 16, 16, 0, 0, 255);
        require(artbox_native_surface_present(first) == ARTBOX_SURFACE_STATE, "reject wrong current surface");
        std::thread worker([&] {
            require(artbox_native_surface_bind(first) == ARTBOX_SURFACE_THREAD, "reject foreign thread bind");
            require(artbox_native_surface_present(first) == ARTBOX_SURFACE_THREAD, "reject foreign thread present");
            require(artbox_native_surface_suspend(first) == ARTBOX_SURFACE_THREAD, "reject foreign thread suspend");
            require(artbox_native_surface_resume(first) == ARTBOX_SURFACE_THREAD, "reject foreign thread resume");
            artbox_native_surface *borrowed = first, *newSurface = nullptr;
            require(artbox_native_surface_destroy(&borrowed) == ARTBOX_SURFACE_THREAD && borrowed == first,
                    "foreign thread cannot destroy owner");
            require(artbox_native_surface_create((void *)secondLayer, &newSurface) == ARTBOX_SURFACE_THREAD && !newSurface,
                    "foreign thread cannot create UI surface");
        });
        worker.join();
        require(artbox_native_surface_suspend(first) == ARTBOX_SURFACE_OK, "suspend noncurrent first surface");
        color(second, 16, 16, 0, 255, 0);
        require(artbox_native_surface_bind(first) == ARTBOX_SURFACE_STATE, "suspended surface cannot bind");
        require(artbox_native_surface_present(first) == ARTBOX_SURFACE_STATE, "suspended surface cannot present");
        require(artbox_native_surface_suspend(first) == ARTBOX_SURFACE_OK, "repeat suspend");
        firstLayer.bounds = CGRectMake(0, 0, 24, 20); firstLayer.contentsScale = 2;
        require(artbox_native_surface_resume(first) == ARTBOX_SURFACE_OK, "resume resized window");
        require(artbox_native_surface_resume(first) == ARTBOX_SURFACE_OK, "repeat resume");
        color(first, 48, 40, 0, 255, 0);
        require(glIsTexture(texture), "suspend/resume preserves context resource");
        GLuint framebuffer; glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "retained texture readable");
        unsigned char actual[4] = {};
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
        require(glGetError() == GL_NO_ERROR && !std::memcmp(texel, actual, 4), "retained texture bytes");
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &framebuffer); glDeleteTextures(1, &texture);
        require(artbox_native_surface_destroy(&second) == ARTBOX_SURFACE_OK && !second,
                "destroy other window without terminating shared display");
        require(artbox_native_surface_present(first) == ARTBOX_SURFACE_OK, "other destruction preserves current binding");
        color(first, 48, 40, 255, 0, 0);
        firstLayer.bounds = CGRectMake(0, 0, 20, 12);
        color(first, 40, 24, 255, 255, 0);
        require(artbox_native_surface_suspend(first) == ARTBOX_SURFACE_OK, "suspend current surface");
        require(artbox_native_surface_destroy(&first) == ARTBOX_SURFACE_OK && !first, "destroy suspended final surface");
        require(artbox_native_surface_destroy(&first) == ARTBOX_SURFACE_OK, "repeat destruction");
        require(artbox_native_surface_create((void *)firstLayer, &first) == ARTBOX_SURFACE_OK, "fresh display after teardown");
        color(first, 40, 24, 0, 0, 255);
        require(artbox_native_surface_destroy(&first) == ARTBOX_SURFACE_OK && !first, "destroy current final surface");
        [firstWindow close]; [secondWindow close]; [firstWindow release]; [secondWindow release];
        NSDictionary *record = @{@"schema": @1, @"backend": @"metal", @"metal_available": @YES,
            @"device": device.name, @"renderer": rendererName, @"verified_pixels": @(pixels_verified),
            @"verified_texture_bytes": @4, @"frames_presented": @(frames_presented),
            @"surfaces_created": @3, @"surfaces_destroyed": @3, @"thread_rejections": @6,
            @"suspend_resume_passed": @YES, @"shared_display_passed": @YES,
            @"elapsed_ms": @((CFAbsoluteTimeGetCurrent() - start) * 1000)};
        NSData *data = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingSortedKeys error:nil];
        require(data != nil, "serialize result");
        std::fwrite(data.bytes, 1, data.length, stdout); std::putchar('\n');
        [device release];
    }
    return 0;
}
