cmake_minimum_required(VERSION 3.10)

# set the project name
project(game)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# add the executable
add_executable(game src/main.cpp)

# add the SDL2 library
# add_subdirectory(lib/SDL2-2.26.1)

target_link_libraries(game PUBLIC
                     "${PROJECT_SOURCE_DIR}/lib/SDL2-2.26.1/lib/x64/SDL2.lib"
                     )
target_link_libraries(game PUBLIC
                     "${PROJECT_SOURCE_DIR}/lib/SDL2-2.26.1/lib/x64/SDL2main.lib"
                     )

# add the binary tree to the search path for include files
# so that we will find TutorialConfig.h
target_include_directories(game PUBLIC
                          "${PROJECT_BINARY_DIR}"
                          "${PROJECT_SOURCE_DIR}/lib/SDL2-2.26.1/include"
                          )
