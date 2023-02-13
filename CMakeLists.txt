cmake_minimum_required(VERSION 3.21)

# set the project name
project(game)

# add the executable
add_executable(game
               src/event.c
               src/filesystem_physfs.c
               src/framework.c
               src/graphics_vulkan.c
               src/main.c
               src/timer.c)

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

# add the Vulkan library
find_package(Vulkan REQUIRED)

# add the GLM library
find_package(glm REQUIRED PATHS lib/glm/cmake)

target_link_libraries(game PUBLIC PhysFS::PhysFS)
target_link_libraries(game PUBLIC SDL2::SDL2)
target_link_libraries(game PUBLIC SDL2::SDL2main)
target_link_libraries(game PUBLIC Vulkan::Headers)
target_link_libraries(game PUBLIC glm::glm)

# add the binary tree to the search path for include files
# so that we will find TutorialConfig.h
target_include_directories(game PUBLIC
                          "${PROJECT_BINARY_DIR}"
                          PhysFS::PhysFS
                          SDL2::SDL2
                          SDL2::SDL2main
                          Vulkan::Headers
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
