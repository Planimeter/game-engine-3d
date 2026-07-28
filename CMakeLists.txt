cmake_minimum_required(VERSION 3.21)

# set the project name
project(game LANGUAGES C CXX OBJC OBJCXX)

# Download and extract CEF if not already present
set(CEF_VERSION "145.0.22+g0fa8d1b+chromium-145.0.7632.45")
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(CEF_PLATFORM "windows64")
    else()
        set(CEF_PLATFORM "windows32")
    endif()
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(CEF_PLATFORM "macosarm64")
    else()
        set(CEF_PLATFORM "macosx64")
    endif()
elseif(UNIX)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(CEF_PLATFORM "linux64")
    else()
        set(CEF_PLATFORM "linuxarm")
    endif()
endif()

# URL encode the + signs in the version string
string(REPLACE "+" "%2B" CEF_VERSION_ENCODED "${CEF_VERSION}")
set(CEF_DOWNLOAD_URL "https://cef-builds.spotifycdn.com/cef_binary_${CEF_VERSION_ENCODED}_${CEF_PLATFORM}.tar.bz2")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef_binary_${CEF_VERSION}_${CEF_PLATFORM}")
    set(CEF_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef_binary_${CEF_VERSION}_${CEF_PLATFORM}.tar.bz2")
    
    if(NOT EXISTS "${CEF_ARCHIVE}")
        message(STATUS "Downloading CEF from ${CEF_DOWNLOAD_URL}")
        
        file(DOWNLOAD
            "${CEF_DOWNLOAD_URL}"
            "${CEF_ARCHIVE}"
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
            TLS_VERIFY ON
            TIMEOUT 600
        )
        
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        list(GET DOWNLOAD_STATUS 1 STATUS_MESSAGE)
        
        if(NOT STATUS_CODE EQUAL 0)
            file(REMOVE "${CEF_ARCHIVE}")
            message(FATAL_ERROR 
                "Failed to download CEF: ${STATUS_MESSAGE}\n"
                "URL: ${CEF_DOWNLOAD_URL}\n"
                "Download manually from https://cef-builds.spotifycdn.com/index.html and place at:\n"
                "${CEF_ARCHIVE}"
            )
        endif()
    endif()
    
    message(STATUS "Extracting CEF to third_party/cef_binary_${CEF_VERSION}_${CEF_PLATFORM}")
    
    file(ARCHIVE_EXTRACT
        INPUT "${CEF_ARCHIVE}"
        DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    )
    
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef_binary_${CEF_VERSION}_${CEF_PLATFORM}")
        message(FATAL_ERROR "CEF extraction failed - directory not found")
    endif()
    
    file(REMOVE "${CEF_ARCHIVE}")
else()
    message(STATUS "Using existing CEF installation")
endif()

# add the PhysFS library
set(PHYSFS_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/physfs-main)
# add the SDL library
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL3-3.2.20)
# add the volk library
add_subdirectory(third_party/volk-1.4.304)
# add the Vulkan Memory Allocator library
add_subdirectory(third_party/VulkanMemoryAllocator-3.3.0)
# add the GLM library
add_subdirectory(third_party/glm)
# add the FreeType library
add_subdirectory(third_party/freetype)
# add the HarfBuzz library
add_subdirectory(third_party/harfbuzz-12.3.2)
# add the shaderc library
# set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
# set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
# set(SHADERC_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
# set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
# add_subdirectory(third_party/shaderc-2026.1)
# add the Assimp library
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/assimp-6.0.4)
# add the OpenAL library
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/openal-soft-1.25.1)
# add the CEF library
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef_binary_${CEF_VERSION}_${CEF_PLATFORM}")

# Disable building CEF test targets by removing the tests directory before
# add_subdirectory is called. The CEF binary distribution's CMakeLists.txt
# unconditionally adds test subdirectories if they exist, with no variable
# to control this behavior. Removing them here prevents compilation errors
# from C++20-incompatible headers in newer toolchains.
if(EXISTS "${CEF_ROOT}/tests")
    file(REMOVE_RECURSE "${CEF_ROOT}/tests")
endif()

# Ensure the CEF wrapper is built as a static library regardless of the
# global BUILD_SHARED_LIBS setting. The CEF distribution expects the
# libcef_dll_wrapper to be a static wrapper; building it as a shared
# library (when BUILD_SHARED_LIBS is ON) causes unresolved externals.
if(DEFINED BUILD_SHARED_LIBS)
    set(_OLD_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
else()
    set(_OLD_BUILD_SHARED_LIBS)
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Temporarily disable shared builds for CEF wrapper" FORCE)
add_subdirectory(third_party/cef_binary_${CEF_VERSION}_${CEF_PLATFORM})
# Restore previous BUILD_SHARED_LIBS value (remove from cache if it wasn't set).
if(_OLD_BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS "${_OLD_BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)
else()
    unset(BUILD_SHARED_LIBS CACHE)
endif()

# Ensure an imported target for the libcef logical library exists so
# `target_link_libraries(game PUBLIC libcef_lib ...)` will find the
# correct import library even if the CEF package didn't create a CMake
# target named `libcef_lib`.
if(NOT TARGET libcef_lib)
    add_library(libcef_lib STATIC IMPORTED)
    set(_CEF_LIB_DEBUG "${CEF_ROOT}/Debug/libcef.lib")
    set(_CEF_LIB_RELEASE "${CEF_ROOT}/Release/libcef.lib")
    set_target_properties(libcef_lib PROPERTIES
        IMPORTED_LOCATION_DEBUG "${_CEF_LIB_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${_CEF_LIB_RELEASE}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${_CEF_LIB_RELEASE}"
        IMPORTED_LOCATION_MINSIZEREL "${_CEF_LIB_RELEASE}"
        IMPORTED_LOCATION "${_CEF_LIB_RELEASE}"
    )
endif()

# specify the list of paths to source files
set(SOURCES
    src/audio_openal.c
    src/event_sdl.c
    src/filesystem_physfs.c
    src/font.c
    src/framework.c
    src/image_stb.c
    src/job_pthread.c
    src/main_sdl.c
    src/math_glm.cpp
    src/model_assimp.cpp
    src/text.c
    src/timer_sdl.c
    src/vk_mem_alloc.cpp
    src/window_sdl.c
    )

# Graphics backend: Metal on macOS, Vulkan elsewhere
if(APPLE)
    list(APPEND SOURCES src/graphics_metal.mm src/graphics_metal_helpers.m)
else()
    list(APPEND SOURCES src/graphics_vulkan.cpp)
endif()

# add the executable
if(WIN32)
    add_executable(game WIN32 ${SOURCES})
elseif(APPLE)
    add_executable(game MACOSX_BUNDLE ${SOURCES})
else()
    add_executable(game ${SOURCES})
endif()

# specify the C++ standard
set(CMAKE_CXX_STANDARD 20)

# specify the C standard
set(CMAKE_C_STANDARD 99)

# https://github.com/KhronosGroup/Vulkan-Samples/blob/3df7dba1b1f428f24cd9a242af78540a518f4b67/third_party/CMakeLists.txt#L53-L86
if(ANDROID)
    target_compile_definitions(game PUBLIC VK_USE_PLATFORM_ANDROID_KHR)
elseif(WIN32)
    target_compile_definitions(game PUBLIC VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    target_compile_definitions(game PUBLIC VK_USE_PLATFORM_METAL_EXT)
elseif(UNIX)
    # Choose WSI based on VKB_WSI_SELECTION
    if (VKB_WSI_SELECTION STREQUAL XCB OR VKB_WSI_SELECTION STREQUAL XLIB OR VKB_WSI_SELECTION STREQUAL WAYLAND)
        find_package(PkgConfig REQUIRED)
    endif()
    if (VKB_WSI_SELECTION STREQUAL XCB)
        pkg_check_modules(XCB xcb REQUIRED)
        if (XCB_FOUND)
            target_compile_definitions(game PUBLIC VK_USE_PLATFORM_XCB_KHR)
        endif()
    elseif (VKB_WSI_SELECTION STREQUAL XLIB)
        pkg_check_modules(X11 x11 REQUIRED)
        if (X11_FOUND)
            target_compile_definitions(game PUBLIC VK_USE_PLATFORM_XLIB_KHR)
        endif()
    elseif (VKB_WSI_SELECTION STREQUAL WAYLAND)
        pkg_check_modules(WAYLAND wayland-client REQUIRED)
        if (WAYLAND_FOUND)
            target_compile_definitions(game PUBLIC VK_USE_PLATFORM_WAYLAND_KHR)
        endif()
    elseif (VKB_WSI_SELECTION STREQUAL D2D)
        set(DIRECT_TO_DISPLAY TRUE)
        set(DIRECT_TO_DISPLAY TRUE PARENT_SCOPE)
        target_compile_definitions(game PUBLIC VK_USE_PLATFORM_DISPLAY_KHR)
    else()
        # UNDONE: No Window System Integration (WSI) on GitHub Actions.
        # message(FATAL_ERROR "Unknown WSI")
    endif()
endif()

set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "Link statically with Vulkan API")
if (APPLE)
    target_compile_options(VulkanMemoryAllocator INTERFACE -Wno-nullability-completeness)
endif()

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

target_link_libraries(game PUBLIC
    PhysFS::PhysFS
    SDL3::SDL3
    volk::volk
    VulkanMemoryAllocator
    glm::glm
    freetype
    harfbuzz
    assimp::assimp
    OpenAL
    libcef_lib
    libcef_dll_wrapper
)

if(NOT WIN32)
    target_link_libraries(game PUBLIC Threads::Threads)
endif()

# Link Windows libraries
if(WIN32)
    target_link_libraries(game PUBLIC
        winmm.lib
        setupapi.lib
        version.lib
        imm32.lib
        crypt32.lib
        wintrust.lib
    )
endif()

# Link macOS frameworks
if(APPLE)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(COREHAPTICS_FRAMEWORK CoreHaptics)
    find_library(COREMEDIA_FRAMEWORK CoreMedia)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    find_library(FORCEFEEDBACK_FRAMEWORK ForceFeedback)
    find_library(GAMECONTROLLER_FRAMEWORK GameController)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(CARBON_FRAMEWORK Carbon)
    find_library(METAL_FRAMEWORK Metal)
    find_library(APPKIT_FRAMEWORK AppKit)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
    find_library(UNIFORMTYPEIDENTIFIERS_FRAMEWORK UniformTypeIdentifiers)

    target_link_libraries(game PUBLIC
        "${AVFOUNDATION_FRAMEWORK}"
        "${COREAUDIO_FRAMEWORK}"
        "${AUDIOTOOLBOX_FRAMEWORK}"
        "${FOUNDATION_FRAMEWORK}"
        "${COREGRAPHICS_FRAMEWORK}"
        "${COREHAPTICS_FRAMEWORK}"
        "${COREMEDIA_FRAMEWORK}"
        "${COREVIDEO_FRAMEWORK}"
        "${FORCEFEEDBACK_FRAMEWORK}"
        "${GAMECONTROLLER_FRAMEWORK}"
        "${IOKIT_FRAMEWORK}"
        "${CARBON_FRAMEWORK}"
        "${METAL_FRAMEWORK}"
        "${APPKIT_FRAMEWORK}"
        "${QUARTZCORE_FRAMEWORK}"
        "${UNIFORMTYPEIDENTIFIERS_FRAMEWORK}"
    )
endif()

# add the binary tree to the search path for include files
# so that we will find TutorialConfig.h
target_include_directories(game PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
                           )

add_custom_command(TARGET game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:game>/shaders
  COMMAND_EXPAND_LISTS
)

# Copy CEF binaries to output directory
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef_binary_${CEF_VERSION}_${CEF_PLATFORM}/Release"
        $<TARGET_FILE_DIR:game>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef_binary_${CEF_VERSION}_${CEF_PLATFORM}/Resources"
        $<TARGET_FILE_DIR:game>
    COMMAND_EXPAND_LISTS
)

# Copy DLLs to output directory
if(WIN32)
    add_custom_command(TARGET game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_RUNTIME_DLLS:game>
            $<TARGET_FILE_DIR:game>
        COMMAND_EXPAND_LISTS
    )
endif()