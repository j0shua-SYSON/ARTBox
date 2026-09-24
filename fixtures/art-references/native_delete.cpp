// SPDX-License-Identifier: MIT
#include <new>

// The test uses the host's actual C++ deallocator for ShadowFrame destruction.
// No STL object or allocated storage crosses between the host and the fixture.
extern "C" void artbox_reference_delete(void* pointer) {
    ::operator delete(pointer);
}
