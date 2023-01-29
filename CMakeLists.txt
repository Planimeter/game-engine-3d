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
add_subdirectory(lib/physfs-release-3.2.0)

# add the SDL2 library
add_subdirectory(lib/SDL2-2.26.2)

# add the GLM library
add_subdirectory(lib/glm-0.9.9.8/glm)

target_link_libraries(game PUBLIC physfs)
target_link_libraries(game PUBLIC SDL2)
target_link_libraries(game PUBLIC SDL2main)
target_link_libraries(game PUBLIC glm)

# add the binary tree to the search path for include files
# so that we will find TutorialConfig.h
target_include_directories(game PUBLIC
                          "${PROJECT_BINARY_DIR}"
                          physfs
                          SDL2
                          SDL2main
                          glm
                          )

if(WIN32)
add_custom_command(TARGET game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:game> $<TARGET_FILE_DIR:game>
  COMMAND_EXPAND_LISTS
)
endif()
