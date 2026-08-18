cmake_minimum_required(VERSION 3.16)
project(fluid_simulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ── GLFW: windowing + OpenGL context ──────────────────────────────────────────
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)

# ── GLAD: OpenGL function loader ──────────────────────────────────────────────
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG        v0.1.36
)

# ── GLM: header-only math library (vectors, matrices, quaternions) ─────────────
# We use GLM for all 3-D math: MVP matrices, camera vectors, etc.
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        0.9.9.8
)

FetchContent_MakeAvailable(glfw glad glm)

find_package(OpenGL REQUIRED)

add_executable(fluid_simulation
  src/main.cpp
  src/FluidSim.cpp
)

target_include_directories(fluid_simulation PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${glad_SOURCE_DIR}/include
  ${glm_SOURCE_DIR}              # GLM is header-only, just needs the include path
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
