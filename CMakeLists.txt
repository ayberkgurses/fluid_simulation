cmake_minimum_required(VERSION 3.16)
project(fluid_simulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG latest
)

FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG v0.1.36
)

FetchContent_MakeAvailable(glfw glad)

find_package(OpenGL REQUIRED)

add_executable(fluid_simulation
  src/main.cpp
  src/FluidSim.cpp
)

target_include_directories(fluid_simulation PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${glad_SOURCE_DIR}/include
)

target_link_libraries(fluid_simulation PRIVATE
  glfw
  glad
  OpenGL::GL
)

target_compile_definitions(fluid_simulation PRIVATE
  GLFW_INCLUDE_NONE
)

if(MSVC)
  target_compile_options(fluid_simulation PRIVATE /W4 /permissive-)
else()
  target_compile_options(fluid_simulation PRIVATE -Wall -Wextra -Wpedantic)
endif()
