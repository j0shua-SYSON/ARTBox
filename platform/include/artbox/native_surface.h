// SPDX-License-Identifier: MIT
#ifndef ARTBOX_NATIVE_SURFACE_H
#define ARTBOX_NATIVE_SURFACE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct artbox_native_surface artbox_native_surface;
typedef enum artbox_surface_result {
    ARTBOX_SURFACE_OK = 0,
    ARTBOX_SURFACE_ARGUMENT,
    ARTBOX_SURFACE_THREAD,
    ARTBOX_SURFACE_UNAVAILABLE,
    ARTBOX_SURFACE_STATE,
    ARTBOX_SURFACE_EGL
} artbox_surface_result;

/* Apple platform boundary, never a guest pointer/allocator boundary. All calls
 * run on the main thread. metal_layer is a live CAMetalLayer with positive
 * bounds and contentsScale; the owner controls its geometry on that thread.
 * Each layer has at most one surface. Creation retains the layer and creates
 * an independent GLES2 context, without changing the currently bound context.
 * out must point to NULL. Failure leaves it NULL. */
artbox_surface_result artbox_native_surface_create(void *metal_layer,
                                                  artbox_native_surface **out);
/* Bind before issuing native ANGLE GLES calls. Present requires this surface
 * to be current. Native EGL/GLES handles never escape this interface. */
artbox_surface_result artbox_native_surface_bind(artbox_native_surface *surface);
artbox_surface_result artbox_native_surface_present(artbox_native_surface *surface);
/* Suspend releases drawables and the window surface, retaining the context
 * and its GL objects. Resume recreates the surface at the current layer size;
 * bind explicitly afterwards. Repeated suspend/resume are harmless. */
artbox_surface_result artbox_native_surface_suspend(artbox_native_surface *surface);
artbox_surface_result artbox_native_surface_resume(artbox_native_surface *surface);
/* Consumes ownership and clears *surface even if EGL teardown reports an
 * error. NULL *surface is harmless. Other surfaces and bindings survive. */
artbox_surface_result artbox_native_surface_destroy(artbox_native_surface **surface);

#ifdef __cplusplus
}
#endif
#endif
