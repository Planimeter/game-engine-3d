cmake_minimum_required(VERSION 3.21)

# set the project name
project(game)

# add the executable
add_executable(game
               src/event_sdl.c
               src/filesystem_physfs.c
               src/framework.c
               src/graphics_vulkan.c
               src/main.c
               src/timer_sdl.c
               src/vk_mem_alloc.cpp
               src/window_sdl.c)

# specify the C++ standard
set_property(TARGET game PROPERTY CXX_EXTENSIONS OFF)
set_property(TARGET game PROPERTY CXX_STANDARD 11)
set_property(TARGET game PROPERTY CXX_STANDARD_REQUIRED ON)

# specify the C standard
set_property(TARGET game PROPERTY C_EXTENSIONS OFF)
set_property(TARGET game PROPERTY C_STANDARD 99)
set_property(TARGET game PROPERTY C_STANDARD_REQUIRED ON)

# add the PhysicsFS library
set(PHYSFS_BUILD_STATIC FALSE CACHE BOOL "Build static library")
set(PHYSFS_BUILD_TEST FALSE CACHE BOOL "Build stdio test program.")
set(PHYSFS_BUILD_DOCS FALSE CACHE BOOL "Build doxygen based documentation")
add_subdirectory(lib/physfs-release-3.2.0)

# add the SDL2 library
find_package(SDL2 2.26.3 EXACT REQUIRED PATHS lib)

# add the volk library
add_subdirectory(lib/volk)

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
        message(FATAL_ERROR "Unknown WSI")
    endif()
endif()

# add the Vulkan Memory Allocator library
add_subdirectory(lib/VulkanMemoryAllocator-3.0.1)
target_compile_definitions(VulkanMemoryAllocator PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0)
target_compile_definitions(VulkanMemoryAllocator PUBLIC VMA_DYNAMIC_VULKAN_FUNCTIONS=0)

# add the GLM library
find_package(glm REQUIRED PATHS lib/glm/cmake)

target_link_libraries(game PUBLIC PhysFS::PhysFS)
target_link_libraries(game PUBLIC SDL2::SDL2)
target_link_libraries(game PUBLIC SDL2::SDL2main)
target_link_libraries(game PUBLIC volk::volk)
target_link_libraries(game PUBLIC VulkanMemoryAllocator)
target_link_libraries(game PUBLIC glm::glm)

# add the binary tree to the search path for include files
# so that we will find TutorialConfig.h
target_include_directories(game PUBLIC
                          "${PROJECT_BINARY_DIR}"
                          PhysFS::PhysFS
                          SDL2::SDL2
                          SDL2::SDL2main
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
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src/shaders $<TARGET_FILE_DIR:game>/shaders
  COMMAND_EXPAND_LISTS
)
endif()
