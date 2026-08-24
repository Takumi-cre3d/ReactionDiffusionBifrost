cmake_minimum_required(VERSION 3.20)
project(ReactionDiffusionCore VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RD_ENABLE_OPENMP "Enable the CPU OpenMP backend" ON)
option(RD_ENABLE_CUDA "Build the optional 2D CUDA backend when nvcc is available" ON)
option(RD_REQUIRE_CUDA "Fail configuration when RD_ENABLE_CUDA is ON but nvcc is unavailable" OFF)
set(
    RD_CUDA_ARCHITECTURES
    "75;86;89"
    CACHE STRING
    "CUDA architectures for release binaries (Turing, Ampere, Ada)"
)

add_library(reaction_diffusion_core INTERFACE)
target_include_directories(
    reaction_diffusion_core
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/native/core"
)

set(RD_CUDA_AVAILABLE OFF)
if(RD_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        add_library(
            reaction_diffusion_cuda
            STATIC
            native/cuda/ReactionDiffusionCuda.cu
        )
        target_include_directories(
            reaction_diffusion_cuda
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/native/core"
        )
        target_compile_definitions(reaction_diffusion_cuda PRIVATE RD_HAS_CUDA=1)
        set_target_properties(
            reaction_diffusion_cuda
            PROPERTIES
                CUDA_ARCHITECTURES "${RD_CUDA_ARCHITECTURES}"
                CUDA_STANDARD 17
                CUDA_STANDARD_REQUIRED ON
                POSITION_INDEPENDENT_CODE ON
        )
        target_compile_definitions(reaction_diffusion_core INTERFACE RD_HAS_CUDA=1)
        target_link_libraries(reaction_diffusion_core INTERFACE reaction_diffusion_cuda)
        set(RD_CUDA_AVAILABLE ON)
        message(STATUS "ReactionDiffusion CUDA backend: enabled (${CMAKE_CUDA_COMPILER})")
    elseif(RD_REQUIRE_CUDA)
        message(FATAL_ERROR "RD_REQUIRE_CUDA is ON, but no CUDA compiler was found.")
    else()
        message(STATUS "ReactionDiffusion CUDA backend: disabled (nvcc not found)")
    endif()
endif()

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
