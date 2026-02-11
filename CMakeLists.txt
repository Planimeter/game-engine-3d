cmake_minimum_required(VERSION 3.21)

# set the project name
project(game)

include(ExternalProject)

add_subdirectory(third_party/physfs-main)
add_subdirectory(third_party/SDL3-3.2.20)
# add the volk library
add_subdirectory(third_party/volk-1.4.304)
# add the Vulkan Memory Allocator library
add_subdirectory(third_party/VulkanMemoryAllocator-3.3.0)
# add the GLM library
# find_package(glm REQUIRED PATHS third_party/glm/cmake)
add_subdirectory(third_party/glm)
# add the Assimp library
add_subdirectory(third_party/assimp-6.0.4)

# specify the list of paths to source files
set(SOURCES
    src/event_sdl.c
    src/filesystem_physfs.c
    src/framework.c
    src/graphics_vulkan.cpp
    src/main_sdl.c
    src/model_assimp.cpp
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
set_property(TARGET game PROPERTY CXX_EXTENSIONS OFF)
set_property(TARGET game PROPERTY CXX_STANDARD 11)
set_property(TARGET game PROPERTY CXX_STANDARD_REQUIRED ON)

# specify the C standard
set_property(TARGET game PROPERTY C_EXTENSIONS OFF)
set_property(TARGET game PROPERTY C_STANDARD 99)
set_property(TARGET game PROPERTY C_STANDARD_REQUIRED ON)

# add the PhysicsFS library using ExternalProject
ExternalProject_Add(PhysFS_external
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/physfs-main
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external/PhysFS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DPHYSFS_BUILD_SHARED=ON
        -DPHYSFS_BUILD_STATIC=OFF
        -DPHYSFS_BUILD_TEST=OFF
        -DPHYSFS_BUILD_DOCS=OFF
    BUILD_ALWAYS FALSE
)

# Create directories first
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/PhysFS/include)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/PhysFS/lib)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/PhysFS/bin)

# Create imported target for PhysFS
add_library(PhysFS::PhysFS SHARED IMPORTED)
if(WIN32)
    set_target_properties(PhysFS::PhysFS PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/PhysFS/bin/physfs.dll
        IMPORTED_IMPLIB ${CMAKE_BINARY_DIR}/external/PhysFS/lib/physfs.lib
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/PhysFS/include
    )
elseif(APPLE)
    set_target_properties(PhysFS::PhysFS PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/PhysFS/lib/libphysfs.dylib
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/PhysFS/include
    )
else()
    set_target_properties(PhysFS::PhysFS PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/PhysFS/lib/libphysfs.so
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/PhysFS/include
    )
endif()
add_dependencies(PhysFS::PhysFS PhysFS_external)

# add the SDL3 library using ExternalProject
ExternalProject_Add(SDL3_external
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3-3.2.20
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external/SDL3
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DSDL_TEST=OFF
        -DSDL_SHARED=ON
        -DSDL_STATIC=OFF
    BUILD_ALWAYS FALSE
)

# Create directories first
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/SDL3/include)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/SDL3/lib)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/SDL3/bin)

# Create imported target for SDL3
add_library(SDL3::SDL3 SHARED IMPORTED)
if(WIN32)
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/SDL3/bin/SDL3.dll
        IMPORTED_IMPLIB ${CMAKE_BINARY_DIR}/external/SDL3/lib/SDL3.lib
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/SDL3/include
    )
elseif(APPLE)
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/SDL3/lib/libSDL3.dylib
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/SDL3/include
    )
else()
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/SDL3/lib/libSDL3.so
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/SDL3/include
    )
endif()
add_dependencies(SDL3::SDL3 SDL3_external)

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

# add the Assimp library using ExternalProject
ExternalProject_Add(Assimp_external
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp-6.0.4
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external/Assimp
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=OFF
        -DASSIMP_BUILD_TESTS=OFF
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
        -DASSIMP_INSTALL_PDB=OFF
        -DASSIMP_NO_EXPORT=ON
    BUILD_ALWAYS FALSE
)

# Create directories first
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/Assimp/include)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/external/Assimp/lib)

# Create imported target for Assimp
add_library(Assimp::Assimp STATIC IMPORTED)
if(WIN32)
    # Try multiple possible library names for Windows
    if(EXISTS ${CMAKE_BINARY_DIR}/external/Assimp/lib/assimp-vc143-mt.lib)
        set(ASSIMP_LIB_PATH ${CMAKE_BINARY_DIR}/external/Assimp/lib/assimp-vc143-mt.lib)
    elseif(EXISTS ${CMAKE_BINARY_DIR}/external/Assimp/lib/assimp.lib)
        set(ASSIMP_LIB_PATH ${CMAKE_BINARY_DIR}/external/Assimp/lib/assimp.lib)
    else()
        set(ASSIMP_LIB_PATH ${CMAKE_BINARY_DIR}/external/Assimp/lib/assimp-vc143-mt.lib)
    endif()
    
    set_target_properties(Assimp::Assimp PROPERTIES
        IMPORTED_LOCATION ${ASSIMP_LIB_PATH}
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/Assimp/include
    )
elseif(APPLE)
    set_target_properties(Assimp::Assimp PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/Assimp/lib/libassimp.a
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/Assimp/include
    )
else()
    set_target_properties(Assimp::Assimp PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/external/Assimp/lib/libassimp.a
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/external/Assimp/include
    )
endif()
add_dependencies(Assimp::Assimp Assimp_external)

target_link_libraries(game PUBLIC
    PhysFS::PhysFS
    SDL3::SDL3
    volk::volk
    VulkanMemoryAllocator
    glm::glm
    Assimp::Assimp
)

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
                           PhysFS::PhysFS
                           SDL3::SDL3
                           volk::volk
                           VulkanMemoryAllocator
                           glm::glm
                           Assimp::Assimp
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
