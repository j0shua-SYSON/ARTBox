// SPDX-License-Identifier: MIT
// Native Apple ANGLE boundary test; no ART or Activity is claimed here.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2.h>
#include <GLSLANG/ShaderLang.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void require(bool condition, const char *what) {
    if (!condition) { std::fprintf(stderr, "ANGLE contract failed: %s (EGL %x, GL %x)\n",
                                 what, eglGetError(), glGetError()); std::exit(1); }
}

static bool translate(GLenum stage, const char *source, bool expected) {
    ShBuiltInResources resources;
    sh::InitBuiltInResources(&resources);
    ShHandle compiler = sh::ConstructCompiler(stage, SH_GLES2_SPEC, SH_MSL_METAL_OUTPUT, &resources);
    require(compiler != nullptr, "construct Metal translator");
    ShCompileOptions options{}; options.objectCode = true;
    bool result = sh::Compile(compiler, &source, 1, options);
    if (result != expected) std::fprintf(stderr, "%s\n", sh::GetInfoLog(compiler).c_str());
    require(result == expected, "shader acceptance/rejection");
    if (expected) require(sh::GetObjectCode(compiler).find("metal") != std::string::npos,
                          "actual Metal shader output");
    else require(!sh::GetInfoLog(compiler).empty(), "invalid shader diagnostic");
    sh::Destruct(compiler);
    return result;
}

int main() {
    @autoreleasepool {
        CFAbsoluteTime start = CFAbsoluteTimeGetCurrent();
        require(sh::Initialize(), "translator initialization");
        translate(GL_VERTEX_SHADER, "attribute vec4 position; void main() { gl_Position = position; }", true);
        translate(GL_FRAGMENT_SHADER, "precision mediump float; void main() { gl_FragColor = vec4(1.0,0.0,0.0,1.0); }", true);
        translate(GL_FRAGMENT_SHADER, "precision mediump float; void main() { gl_FragColor = missing_name; }", false);
        require(sh::Finalize(), "translator shutdown");
        NSMutableDictionary *record = [@{@"schema": @1, @"translated_shaders": @2,
            @"invalid_shader_rejected": @YES, @"verified_pixels": @0} mutableCopy];
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        record[@"metal_available"] = @(device != nil);
        if (device) {
            record[@"device"] = device.name;
            const EGLint attributes[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE};
            auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
            require(getDisplay != nullptr, "ANGLE platform entry");
            EGLDisplay display = getDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr, attributes);
            require(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr), "Metal EGL initialization");
            require(eglBindAPI(EGL_OPENGL_ES_API), "GLES API binding");
            const EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
            EGLConfig config; EGLint count = 0;
            require(eglChooseConfig(display, configAttributes, &config, 1, &count) && count == 1, "RGBA pbuffer config");
            const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
            EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
            const EGLint surfaceAttributes[] = {EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE};
            EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
            require(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE, "context and surface creation");
            require(eglMakeCurrent(display, surface, surface, context), "current Metal context");
            const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
            require(renderer && std::strstr(renderer, "Metal"), "Metal renderer identity");
            record[@"renderer"] = @(renderer); record[@"backend"] = @"metal";
            unsigned char pixels[32 * 32 * 4];
            for (int phase = 0; phase < 2; ++phase) {
                glClearColor(phase == 0 ? 1.0f : 0.0f, phase == 1 ? 1.0f : 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                std::memset(pixels, 0xcc, sizeof(pixels));
                glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                require(glGetError() == GL_NO_ERROR, "clear and readback");
                for (int p = 0; p < 1024; ++p) require(pixels[p*4] == (phase == 0 ? 255 : 0) &&
                    pixels[p*4+1] == (phase == 1 ? 255 : 0) && pixels[p*4+2] == 0 && pixels[p*4+3] == 255,
                    "exact pixel value after color change");
            }
            require(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "release current context");
            require(eglDestroySurface(display, surface), "destroy surface");
            require(eglDestroyContext(display, context), "destroy context");
            require(eglTerminate(display), "terminate display");
            require(eglReleaseThread(), "release EGL thread");
            record[@"verified_pixels"] = @2048; record[@"surfaces_destroyed"] = @1; record[@"contexts_destroyed"] = @1;
            [device release];
        }
        record[@"elapsed_ms"] = @((CFAbsoluteTimeGetCurrent()-start)*1000.0);
        NSData *data = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingSortedKeys error:nil];
        require(data != nil, "result serialization");
        std::fwrite(data.bytes, 1, data.length, stdout); std::putchar('\n');
        [record release];
    }
    return 0;
}
