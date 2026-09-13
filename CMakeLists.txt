cmake_minimum_required(VERSION 3.24)
project(ARTBox VERSION 0.0.1 LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build, for example cmake -S . -B build/host")
endif()
set(ARTBOX_BUNDLE_ID "com.j0shua.ARTBox" CACHE STRING "iOS application bundle identifier")
set(ARTBOX_GUEST_BUNDLES "" CACHE PATH "Prepared and signed M1 framework directory")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON)
set(CMAKE_EXPORT_PACKAGE_REGISTRY OFF)

add_library(artbox_core STATIC core/src/runtime.c core/src/elf.c core/src/guest.c)
target_include_directories(artbox_core PUBLIC core/include)
add_library(artbox_platform STATIC platform/native_memory.c)
target_include_directories(artbox_platform PUBLIC platform/include)
target_link_libraries(artbox_platform PUBLIC artbox_core)
if(APPLE AND (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" OR
             CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
    enable_language(ASM)
    target_sources(artbox_platform PRIVATE platform/apple/enter_arm64.S platform/apple/native_hello.c)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(artbox_platform PRIVATE _DEFAULT_SOURCE)
endif()
if(MSVC)
    target_compile_options(artbox_core PUBLIC /W4 /WX)
else()
    target_compile_options(artbox_core PUBLIC -Wall -Wextra -Wpedantic -Werror)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" OR
       CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "15.0")
        message(FATAL_ERROR "The app requires arm64 and iOS 15.0 or later")
    endif()
    enable_language(OBJC)
    set(CMAKE_XCODE_GENERATE_SCHEME ON)
    add_executable(ARTBox MACOSX_BUNDLE app/main.m app/ConsoleViewController.m)
    target_link_libraries(ARTBox PRIVATE artbox_core
        "-framework UIKit" "-framework Foundation" "-framework CoreGraphics")
    if(ARTBOX_GUEST_BUNDLES)
        target_link_libraries(ARTBox PRIVATE artbox_platform)
        target_compile_definitions(ARTBox PRIVATE ARTBOX_M1=1)
        target_include_directories(ARTBox PRIVATE "${ARTBOX_GUEST_BUNDLES}/pack")
        foreach(kind Converted Wrapped)
            add_custom_command(TARGET ARTBox POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${ARTBOX_GUEST_BUNDLES}/ARTBox${kind}.framework"
                    "$<TARGET_BUNDLE_DIR:ARTBox>/Frameworks/ARTBox${kind}.framework"
                VERBATIM)
        endforeach()
    endif()
    target_compile_options(ARTBox PRIVATE -fobjc-arc -Werror=unguarded-availability-new)
    set_target_properties(ARTBox PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/app/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${ARTBOX_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "ARTBox"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${ARTBOX_BUNDLE_ID}"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/app/ARTBox.entitlements"
        XCODE_ATTRIBUTE_CLANG_MODULE_CACHE_PATH "$ENV{CLANG_MODULE_CACHE_PATH}"
        XCODE_ATTRIBUTE_ALWAYS_SEARCH_USER_PATHS "NO"
    )
else()
    if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        add_executable(artbox_native_hello tests/native_hello.c)
        target_link_libraries(artbox_native_hello PRIVATE artbox_platform)
    endif()
    add_executable(artbox_host platform/host/main.c)
    target_link_libraries(artbox_host PRIVATE artbox_core)
    enable_testing()
    add_executable(test_runtime tests/test_runtime.c)
    target_link_libraries(test_runtime PRIVATE artbox_core)
    add_test(NAME runtime_contract COMMAND test_runtime)
    add_executable(test_elf tests/test_elf.c)
    target_link_libraries(test_elf PRIVATE artbox_core)
    add_test(NAME elf_validation COMMAND test_elf)
    add_executable(test_syscalls tests/test_syscalls.c)
    target_link_libraries(test_syscalls PRIVATE artbox_platform)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(test_syscalls PRIVATE _DEFAULT_SOURCE)
    endif()
    add_test(NAME syscall_contract COMMAND test_syscalls)
    add_test(NAME host_startup COMMAND "${CMAKE_COMMAND}"
        "-DHOST_EXECUTABLE=$<TARGET_FILE:artbox_host>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_host.cmake")
    set_tests_properties(runtime_contract host_startup elf_validation syscall_contract PROPERTIES TIMEOUT 15)
endif()
