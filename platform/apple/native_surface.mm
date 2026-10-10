// SPDX-License-Identifier: MIT
#include "artbox/native_surface.h"
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <cmath>
#include <new>

struct artbox_native_surface {
    CAMetalLayer *layer;
    EGLContext context;
    EGLSurface window;
    artbox_native_surface *next;
};

// This module exclusively owns the native ANGLE display. Every access is on
// the main thread, including ownership changes; no guest C++ state crosses it.
static EGLDisplay display = EGL_NO_DISPLAY;
static EGLConfig config;
static artbox_native_surface *surfaces;

static bool geometry(CAMetalLayer *layer) {
    const CGSize size = layer.bounds.size;
    const double scale = layer.contentsScale;
    return std::isfinite(scale) && scale > 0 && std::isfinite(size.width) &&
        std::isfinite(size.height) && size.width * scale >= 1 && size.height * scale >= 1 &&
        size.width * scale <= 16384 && size.height * scale <= 16384;
}

static artbox_surface_result checked(artbox_native_surface *surface) {
    if (![NSThread isMainThread]) return ARTBOX_SURFACE_THREAD;
    return surface ? ARTBOX_SURFACE_OK : ARTBOX_SURFACE_ARGUMENT;
}

static bool current(const artbox_native_surface *surface) {
    return eglGetCurrentDisplay() == display && eglGetCurrentContext() == surface->context;
}

static bool release_current(const artbox_native_surface *surface) {
    return !current(surface) || eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

static artbox_surface_result initialize() {
    if (display != EGL_NO_DISPLAY) return ARTBOX_SURFACE_OK;
    const EGLint attributes[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE};
    auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!getDisplay) return ARTBOX_SURFACE_UNAVAILABLE;
    EGLDisplay candidate = getDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr, attributes);
    if (candidate == EGL_NO_DISPLAY || !eglInitialize(candidate, nullptr, nullptr)) return ARTBOX_SURFACE_UNAVAILABLE;
    const EGLint options[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLint count = 0;
    if (!eglBindAPI(EGL_OPENGL_ES_API) || !eglChooseConfig(candidate, options, &config, 1, &count) || count != 1) {
        eglTerminate(candidate);
        return ARTBOX_SURFACE_EGL;
    }
    display = candidate;
    return ARTBOX_SURFACE_OK;
}

static bool terminate_if_empty() {
    if (surfaces || display == EGL_NO_DISPLAY) return true;
    bool okay = eglTerminate(display);
    display = EGL_NO_DISPLAY;
    return okay;
}

artbox_surface_result artbox_native_surface_create(void *metal_layer, artbox_native_surface **out) {
    if (![NSThread isMainThread]) return ARTBOX_SURFACE_THREAD;
    if (!out || *out || !metal_layer) return ARTBOX_SURFACE_ARGUMENT;
    CAMetalLayer *layer = static_cast<CAMetalLayer *>(metal_layer);
    if (![layer isKindOfClass:CAMetalLayer.class] || !geometry(layer)) return ARTBOX_SURFACE_ARGUMENT;
    for (auto item = surfaces; item; item = item->next)
        if (item->layer == layer) return ARTBOX_SURFACE_STATE;
    auto surface = new (std::nothrow) artbox_native_surface{};
    if (!surface) return ARTBOX_SURFACE_UNAVAILABLE;
    artbox_surface_result result = initialize();
    if (result != ARTBOX_SURFACE_OK) { delete surface; return result; }
    const EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    surface->context = eglCreateContext(display, config, EGL_NO_CONTEXT, attributes);
    surface->window = surface->context == EGL_NO_CONTEXT ? EGL_NO_SURFACE :
        eglCreateWindowSurface(display, config, reinterpret_cast<EGLNativeWindowType>(layer), nullptr);
    if (surface->context == EGL_NO_CONTEXT || surface->window == EGL_NO_SURFACE) {
        if (surface->context != EGL_NO_CONTEXT) eglDestroyContext(display, surface->context);
        delete surface;
        terminate_if_empty();
        return ARTBOX_SURFACE_EGL;
    }
    surface->layer = [layer retain];
    surface->next = surfaces;
    surfaces = surface;
    *out = surface;
    return ARTBOX_SURFACE_OK;
}

artbox_surface_result artbox_native_surface_bind(artbox_native_surface *surface) {
    artbox_surface_result result = checked(surface);
    if (result != ARTBOX_SURFACE_OK) return result;
    if (surface->window == EGL_NO_SURFACE || !geometry(surface->layer)) return ARTBOX_SURFACE_STATE;
    return eglMakeCurrent(display, surface->window, surface->window, surface->context) ?
        ARTBOX_SURFACE_OK : ARTBOX_SURFACE_EGL;
}

artbox_surface_result artbox_native_surface_present(artbox_native_surface *surface) {
    artbox_surface_result result = checked(surface);
    if (result != ARTBOX_SURFACE_OK) return result;
    if (surface->window == EGL_NO_SURFACE || !current(surface) || !geometry(surface->layer)) return ARTBOX_SURFACE_STATE;
    return eglSwapBuffers(display, surface->window) ? ARTBOX_SURFACE_OK : ARTBOX_SURFACE_EGL;
}

artbox_surface_result artbox_native_surface_suspend(artbox_native_surface *surface) {
    artbox_surface_result result = checked(surface);
    if (result != ARTBOX_SURFACE_OK) return result;
    if (surface->window == EGL_NO_SURFACE) return ARTBOX_SURFACE_OK;
    if (!release_current(surface) || !eglDestroySurface(display, surface->window)) return ARTBOX_SURFACE_EGL;
    surface->window = EGL_NO_SURFACE;
    return ARTBOX_SURFACE_OK;
}

artbox_surface_result artbox_native_surface_resume(artbox_native_surface *surface) {
    artbox_surface_result result = checked(surface);
    if (result != ARTBOX_SURFACE_OK) return result;
    if (surface->window != EGL_NO_SURFACE) return ARTBOX_SURFACE_OK;
    if (!geometry(surface->layer)) return ARTBOX_SURFACE_STATE;
    surface->window = eglCreateWindowSurface(display, config, reinterpret_cast<EGLNativeWindowType>(surface->layer), nullptr);
    return surface->window != EGL_NO_SURFACE ? ARTBOX_SURFACE_OK : ARTBOX_SURFACE_EGL;
}

artbox_surface_result artbox_native_surface_destroy(artbox_native_surface **owner) {
    if (![NSThread isMainThread]) return ARTBOX_SURFACE_THREAD;
    if (!owner) return ARTBOX_SURFACE_ARGUMENT;
    auto surface = *owner;
    if (!surface) return ARTBOX_SURFACE_OK;
    bool okay = release_current(surface);
    if (surface->window != EGL_NO_SURFACE && !eglDestroySurface(display, surface->window)) okay = false;
    if (!eglDestroyContext(display, surface->context)) okay = false;
    auto link = &surfaces;
    while (*link && *link != surface) link = &(*link)->next;
    if (*link) *link = surface->next;
    [surface->layer release];
    delete surface;
    *owner = nullptr;
    if (!terminate_if_empty()) okay = false;
    return okay ? ARTBOX_SURFACE_OK : ARTBOX_SURFACE_EGL;
}
