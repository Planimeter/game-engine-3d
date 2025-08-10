cmake_minimum_required(VERSION 3.21)

# set the project name
project(game)

include(ExternalProject)

# specify the list of paths to source files
set(SOURCES
    src/event_sdl.c
    src/filesystem_physfs.c
    src/framework.c
    src/graphics_vulkan.cpp
    src/main_sdl.c
    src/timer_sdl.c
    src/vk_mem_alloc.cpp
    src/window_sdl.c
    )

# add the executable
if (WIN32)
    add_executable(game WIN32 ${SOURCES})
elseif(APPLE)
    add_executable(game MACOSX_BUNDLE ${SOURCES})
else()
    add_executable(game ${SOURCES})
endif()

# specify the C++ standard
set_property(TARGET game PROPERTY CXX_EXTENSIONS OFF)
set_property(TARGET game PROPERTY CXX_STANDARD 11)
set_property(TARGET game PROPERTY CXX_STANDARD_REQUIRED ON)

# specify the C standard
set_property(TARGET game PROPERTY C_EXTENSIONS OFF)
set_property(TARGET game PROPERTY C_STANDARD 99)
set_property(TARGET game PROPERTY C_STANDARD_REQUIRED ON)

# add the PhysicsFS library using ExternalProject
ExternalProject_Add(PhysFS_external
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/physfs-main
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external/PhysFS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DPHYSFS_BUILD_STATIC=ON
        -DPHYSFS_BUILD_TEST=OFF
        -DPHYSFS_BUILD_DOCS=OFF
    BUILD_ALWAYS FALSE
)

# Create directories first
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/PhysFS/include)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/PhysFS/lib)

# Create imported target for PhysFS
add_library(PhysFS::PhysFS STATIC IMPORTED)
set_target_properties(PhysFS::PhysFS PROPERTIES
    IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/PhysFS/lib/libphysfs.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/PhysFS/include
)
add_dependencies(PhysFS::PhysFS PhysFS_external)

# add the SDL3 library using ExternalProject
ExternalProject_Add(SDL3_external
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/SDL3-3.2.20
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external/SDL3
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DSDL_TEST=OFF
        -DSDL_STATIC=ON
    BUILD_ALWAYS FALSE
)

# Create directories first
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/SDL3/include)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/SDL3/lib)

# Create imported target for SDL3
add_library(SDL3::SDL3 STATIC IMPORTED)
set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/SDL3/lib/libSDL3.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/SDL3/include
)
add_dependencies(SDL3::SDL3 SDL3_external)

# add the volk library
add_subdirectory(lib/volk-1.4.304)

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

# add the Vulkan Memory Allocator library
set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "Link statically with Vulkan API")
add_subdirectory(lib/VulkanMemoryAllocator-3.3.0)
# if (APPLE)
#     target_compile_options(VulkanMemoryAllocator PUBLIC -Wno-nullability-completeness)
# endif()

# add the GLM library
# find_package(glm REQUIRED PATHS lib/glm/cmake)
add_subdirectory(lib/glm)

target_link_libraries(game PUBLIC PhysFS::PhysFS)
target_link_libraries(game PUBLIC SDL3::SDL3)
target_link_libraries(game PUBLIC volk::volk)
target_link_libraries(game PUBLIC VulkanMemoryAllocator)
target_link_libraries(game PUBLIC glm::glm)

# Link macOS frameworks
if(APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)
    find_library(COREHAPTICS_FRAMEWORK CoreHaptics REQUIRED)
    find_library(FORCEFEEDBACK_FRAMEWORK ForceFeedback REQUIRED)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(CARBON_FRAMEWORK Carbon REQUIRED)
    find_library(UNIFORMTYPEIDENTIFIERS_FRAMEWORK UniformTypeIdentifiers REQUIRED)
    find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
    
    target_link_libraries(game PUBLIC
        ${FOUNDATION_FRAMEWORK}
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${AVFOUNDATION_FRAMEWORK}
        ${GAMECONTROLLER_FRAMEWORK}
        ${COREHAPTICS_FRAMEWORK}
        ${FORCEFEEDBACK_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${CARBON_FRAMEWORK}
        ${UNIFORMTYPEIDENTIFIERS_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
    )
endif()

# add the binary tree to the search path for include files
# so that we will find TutorialConfig.h
target_include_directories(game PUBLIC
                           "${PROJECT_BINARY_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/lib"
                           PhysFS::PhysFS
                           SDL3::SDL3
                           volk::volk
                           VulkanMemoryAllocator
                           glm::glm
                           )

if(WIN32)
add_custom_command(TARGET game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:game> $<TARGET_FILE_DIR:game>
  COMMAND_EXPAND_LISTS
)

add_custom_command(TARGET game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:game>/shaders
  COMMAND_EXPAND_LISTS
)
elseif(APPLE)
add_custom_command(TARGET game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:game>/shaders
  COMMAND_EXPAND_LISTS
)
endif()
