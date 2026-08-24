cmake_minimum_required(VERSION 3.20)
project(ReactionDiffusionCore VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RD_ENABLE_OPENMP "Enable the CPU OpenMP backend" ON)

add_library(reaction_diffusion_core INTERFACE)
target_include_directories(
    reaction_diffusion_core
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/native/core"
)

if(RD_ENABLE_OPENMP)
    find_package(OpenMP QUIET)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(reaction_diffusion_core INTERFACE OpenMP::OpenMP_CXX)
        target_compile_definitions(reaction_diffusion_core INTERFACE RD_HAS_OPENMP=1)
    endif()
endif()

if(MSVC)
    target_compile_options(reaction_diffusion_core INTERFACE /W4 /permissive-)
else()
    target_compile_options(reaction_diffusion_core INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_executable(rd_core_tests native/tools/rd_core_tests.cpp)
target_link_libraries(rd_core_tests PRIVATE reaction_diffusion_core)

add_executable(rd_benchmark native/tools/rd_benchmark.cpp)
target_link_libraries(rd_benchmark PRIVATE reaction_diffusion_core)

add_executable(rd_reference_image native/tools/rd_reference_image.cpp)
target_link_libraries(rd_reference_image PRIVATE reaction_diffusion_core)

enable_testing()
add_test(NAME reaction_diffusion_core COMMAND rd_core_tests)
