cmake_minimum_required(VERSION 3.22)

# set the project name
project(game LANGUAGES C CXX OBJC OBJCXX)

# Force dynamic runtime (/MD) for MSVC - set after project() for proper detection
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "" FORCE)
    # Force RelWithDebInfo to prevent third-party libraries from injecting /MTd
    # (e.g. Assimp uses $<$<CONFIG:Debug>:/MTd> which activates when Debug)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

# Enable CMP0091 to allow CMAKE_MSVC_RUNTIME_LIBRARY to propagate to FetchContent subprojects
cmake_policy(SET CMP0091 NEW)

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
# Fix harfbuzz on MSVC: detect inttypes.h and provide format macro fallbacks.
# Without this, hb-common.h may fail to compile because PRIu64/PRId64/PRIx64
# are not defined when inttypes.h is missing.
include(CheckIncludeFile)
check_include_file(inttypes.h HAVE_INTTYPES_H)
if(HAVE_INTTYPES_H)
    target_compile_definitions(harfbuzz PUBLIC HAVE_INTTYPES_H)
elseif(MSVC)
    target_compile_definitions(harfbuzz PUBLIC
        "PRIu64=\"llu\""
        "PRId64=\"lld\""
        "PRIx64=\"llx\""
    )
endif()
# add the shaderc library
include(FetchContent)

# shaderc requires glslang, SPIRV-Headers, and SPIRV-Tools
# which are not vendored — fetch them at configure time.
# Use FetchContent_Populate (not MakeAvailable) to avoid creating targets
# before shaderc's third_party/CMakeLists.txt adds them as subdirectories.
# NOTE: Avoid hyphens in FetchContent names — CMake cannot dereference
# variables like ${SPIRV-Tools_SOURCE_DIR} (hyphen = subtraction operator).
set(SHADERC_GLSLANG_REVISION "f0bd0257c308b9a26562c1a30c4748a0219cc951")
set(SHADERC_SPIRV_HEADERS_REVISION "04f10f650d514df88b76d25e83db360142c7b174")
set(SHADERC_SPIRV_TOOLS_REVISION "fbe4f3ad913c44fe8700545f8ffe35d1382b7093")

FetchContent_Declare(
  glslang
  GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
  GIT_TAG ${SHADERC_GLSLANG_REVISION}
  CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/msvc_toolchain.cmake -DENABLE_OPT=OFF
)
FetchContent_Declare(
  spirv_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
  GIT_TAG ${SHADERC_SPIRV_HEADERS_REVISION}
  CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/msvc_toolchain.cmake
)
FetchContent_Declare(
  spirv_tools
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
  GIT_TAG ${SHADERC_SPIRV_TOOLS_REVISION}
  CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/msvc_toolchain.cmake -DSPIRV_TOOLS_BUILD_TOOLS=OFF -DSPIRV_TOOLS_BUILD_FUZZ=OFF -DSPIRV_TOOLS_BUILD_TESTS=OFF -DSPIRV_TOOLS_BUILD_EXES=OFF -DSPIRV_WERROR=OFF -DSPIRV_TOOLS_BUILD_STATIC=OFF -DSPIRV_TOOLS_LIBRARY_TYPE=SHARED -DBUILD_SHARED_LIBS=ON -DSPIRV_SKIP_STATIC_LIBRARY=ON
)

# Download only — do not add_subdirectory (shaderc handles that).
FetchContent_Populate(glslang)
FetchContent_Populate(spirv_headers)
FetchContent_Populate(spirv_tools)

# Point shaderc's third_party resolver at the fetched sources directly
set(SHADERC_GLSLANG_DIR "${glslang_SOURCE_DIR}" CACHE INTERNAL "")
set(SHADERC_SPIRV_HEADERS_DIR "${spirv_headers_SOURCE_DIR}" CACHE INTERNAL "")
set(SHADERC_SPIRV_TOOLS_DIR "${spirv_tools_SOURCE_DIR}" CACHE INTERNAL "")
# SPIRV-Tools' external/CMakeLists.txt checks SPIRV-Headers_SOURCE_DIR
# (with hyphen). Set it manually since we use underscores for FetchContent.
set("SPIRV-Headers_SOURCE_DIR" "${spirv_headers_SOURCE_DIR}" CACHE INTERNAL "")

# glslang's ENABLE_OPT defaults to ON but requires SPIRV-Tools-opt,
# which we don't have as a standalone target. Disable it.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)

set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
# Force shaderc to use shared CRT (/MD) instead of static (/MT)
set(SHADERC_ENABLE_SHARED_CRT ON CACHE BOOL "" FORCE)

# SPIRV-Tools: FetchContent_Populate only downloads sources — it never runs
# cmake configure, so the CMAKE_ARGS we passed earlier are ignored. Set these
# as cache variables here so they take effect when shaderc's third_party/
# CMakeLists.txt does add_subdirectory() on the spirv-tools source tree.
#
# Use STATIC for everything.  With SPIRV_TOOLS_BUILD_STATIC=ON:
#   SPIRV-Tools-static  = STATIC lib (full symbol visibility)
#   SPIRV-Tools-shared  = SHARED lib (hidden symbol visibility)
#   SPIRV-Tools         = alias → SPIRV-Tools-static
# shaderc's SHADERC_LIBS links SPIRV-Tools-shared, but after add_subdirectory
# we patch the shaderc targets to use SPIRV-Tools (static) instead.
#
# FORCE is required because a previous configure may have cached OFF.
set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_BUILD_FUZZ OFF CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_BUILD_EXES OFF CACHE BOOL "" FORCE)
set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)

add_subdirectory(third_party/shaderc-2026.1)

# Strip every reference to SPIRV-Tools-shared (the SHARED library) from all
# targets, so that only SPIRV-Tools (alias -> SPIRV-Tools-static) remains.
# This avoids LNK2005 duplicate-symbol errors when both a static .lib and an
# import library for the shared DLL land on the same link line.
if(TARGET SPIRV-Tools-shared)
  set_target_properties(SPIRV-Tools-shared PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# Helper: remove SPIRV-Tools-shared from a target's LINK_LIBRARIES.
macro(_strip_shared_spv_tools _tgt)
  if(TARGET ${_tgt})
    get_target_property(_libs ${_tgt} LINK_LIBRARIES)
    if(_libs)
      set(_new_libs "")
      set(_dirty FALSE)
      foreach(_lib IN LISTS _libs)
        if("${_lib}" MATCHES "SPIRV-Tools-shared")
          set(_dirty TRUE)
        else()
          list(APPEND _new_libs "${_lib}")
        endif()
      endforeach()
      if(_dirty)
        set_target_properties(${_tgt} PROPERTIES LINK_LIBRARIES "${_new_libs}")
      endif()
    endif()
    get_target_property(_ilibs ${_tgt} INTERFACE_LINK_LIBRARIES)
    if(_ilibs)
      set(_new_libs "")
      set(_dirty FALSE)
      foreach(_lib IN LISTS _ilibs)
        if("${_lib}" MATCHES "SPIRV-Tools-shared")
          set(_dirty TRUE)
        else()
          list(APPEND _new_libs "${_lib}")
        endif()
      endforeach()
      if(_dirty)
        set_target_properties(${_tgt} PROPERTIES INTERFACE_LINK_LIBRARIES "${_new_libs}")
      endif()
    endif()
  endif()
endmacro()

foreach(_tgt shaderc shaderc_shared shaderc_util glslang SPIRV-Tools-opt)
  _strip_shared_spv_tools(${_tgt})
endforeach()
# add the Assimp library
if (MSVC)
    # Assimp uses C++ exception handlers without /EHsc, causing C4530 treated as error with /WX
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/assimp-6.0.4)
# Force Assimp to use release CRT regardless of its own generator expressions
if(TARGET assimp)
    set_target_properties(assimp PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
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

# Graphics backend: Metal on macOS, Vulkan elsewhere (or OpenGL if USE_OPENGL is set)
option(USE_OPENGL "Use OpenGL backend instead of Vulkan on non-macOS platforms" OFF)
if(APPLE)
    list(APPEND SOURCES src/graphics_metal.mm src/graphics_metal_helpers.m)
elseif(USE_OPENGL)
    list(APPEND SOURCES src/graphics_opengl_sdl.c)
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
if(NOT USE_OPENGL)
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
endif()

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

if(USE_OPENGL)
    target_link_libraries(game PUBLIC
        PhysFS::PhysFS
        SDL3::SDL3
        glm::glm
        freetype
        harfbuzz
        assimp::assimp
        OpenAL
        libcef_lib
        libcef_dll_wrapper
    )
    if(NOT WIN32)
        find_package(OpenGL REQUIRED)
        target_link_libraries(game PUBLIC OpenGL::GL)
    endif()
else()
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
        OpenAL
        libcef_lib
        libcef_dll_wrapper
    )
endif()

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