cmake_minimum_required(VERSION 3.21)

# set the project name
project(game LANGUAGES C CXX)

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
add_subdirectory(third_party/shaderc-2026.1)
# add the Assimp library
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/assimp-6.0.4)

# specify the list of paths to source files
set(SOURCES
    src/event_sdl.c
    src/filesystem_physfs.c
    src/font.c
    src/framework.c
    src/graphics_vulkan.cpp
    src/job_pthread.c
    src/main_sdl.c
    src/model_assimp.cpp
    src/text.c
    src/timer_sdl.c
    src/vk_mem_alloc.cpp
    src/window_sdl.c
    )

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
    shaderc
    assimp::assimp
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

# Copy DLLs to output directory
if(WIN32)
    add_custom_command(TARGET game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_RUNTIME_DLLS:game>
            $<TARGET_FILE_DIR:game>
        COMMAND_EXPAND_LISTS
    )
endif()
