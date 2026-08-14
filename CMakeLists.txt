# cheatah-plot — cross-platform GPU plotting for cheatah.
#
# Draw a plot in a few lines, on whatever GPU you have. cheatah-plot renders through cheatah-gpu's easy,
# cross-platform `gpu` layer (which composes the 1:1 gpu.vulkan / gpu.metal surfaces) and presents to a
# GLFW window OR to an offscreen framebuffer you can read back (for streaming a live plot — e.g. to a
# website, without JavaScript). The library + its renderer are authored in cheatah (.purr) as much as
# possible; only a thin C++ shim binds the windowing library.
#
# The `plot` package is surfaced as generated, header-only C++20 (from the `.purr` sources via
# `purrc --emit-library`, kept in sync by scripts/gen-headers.sh) in the `cheatah::plot::*` namespaces.
# A downstream project opts in with `biome add cheatah-plot`, then `import plot`.
cmake_minimum_required(VERSION 3.24)
project(cheatah-plot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- The `plot` package, surfaced as a header-only INTERFACE target ----------------------------
# Consumers resolve `import plot` from the generated headers on purrc's import path; nothing to compile
# or link into the package itself. The include dir is the repo root so `plot/plot.hpp` & the submodule
# headers resolve exactly as purrc expects.
add_library(cheatah_plot INTERFACE)
add_library(cheatah::plot ALIAS cheatah_plot)
target_include_directories(cheatah_plot INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # The package dir itself: generated submodule headers include the umbrella as "plot.hpp"
    # (the module-path spelling purrc emits for cross-module imports).
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/plot>)
target_compile_features(cheatah_plot INTERFACE cxx_std_20)

# Everything below is for building/testing THIS repo standalone; when cheatah-plot is fetched as a
# sub-project (the biome path) the parent only wants the headers above.
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

# A single, predictable output tree (so scripts/coverage.sh finds ./bin/cheatah_plot_tests).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Standalone dev: wire git hooks so a push runs the qa_gate ---------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git" OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.git")
    execute_process(
        COMMAND git config core.hooksPath .githooks
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_QUIET ERROR_QUIET)
    message(STATUS "cheatah-plot: git pre-push hook -> scripts/qa_gate.sh")
endif()

# Sibling checkouts the qa_gate needs: the cheatah toolchain (purrc + cheatah runtime) and cheatah-gpu
# (the easy `gpu` layer cheatah-plot renders through). Override either to relocate it.
set(CHEATAH_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/../cheatah"     CACHE PATH "cheatah toolchain checkout")
set(CHEATAH_GPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cheatah-gpu" CACHE PATH "cheatah-gpu extension checkout")

# --- Options -----------------------------------------------------------------------------------
option(CHEATAH_PLOT_BUILD_TESTS  "Build the C++ unit tests (fetches GoogleTest)" OFF)
option(CHEATAH_PLOT_SANITIZE     "Build the unit tests with ASan + UBSan" OFF)
option(CHEATAH_PLOT_BUILD_VULKAN "Build the window/present tests (need GLFW + a GPU)" OFF)

# --- GPU lanes: the Slang kernels compiled per backend + the device stacks ----------------------
# The renderer's GPU path is OPTIONAL. With slangc (ships in the Vulkan SDK) and a cheatah-gpu
# checkout, shaders/plot.slang is compiled at build time — one SPIR-V module per entry point for
# the Vulkan lane, and the Metal source per entry on EVERY platform (so a broken metal target
# fails here, not on the first Apple machine) — and the `cheatah_plot_gpu` INTERFACE target
# carries the enabled lanes' includes/defines/sources for the one test binary. Without slangc
# (or without cheatah-gpu) the GPU lane is disabled cleanly: the CPU tests still build and run.
set(CHEATAH_PLOT_GPU_LANES "")

# The Vulkan SDK: $VULKAN_SDK (the SDK's setup-env.sh) wins; otherwise the newest
# ~/Tools/vulkan-sdk install — the same discovery cheatah-gpu's gates use.
if(DEFINED ENV{VULKAN_SDK})
    set(_vk_root "$ENV{VULKAN_SDK}")
else()
    file(GLOB _vk_candidates "$ENV{HOME}/Tools/vulkan-sdk/*/x86_64" "$ENV{HOME}/VulkanSDK/*/x86_64")
    list(SORT _vk_candidates)
    list(POP_BACK _vk_candidates _vk_root)
endif()
set(CHEATAH_PLOT_VK_ROOT "${_vk_root}" CACHE PATH "Vulkan SDK root (include/, lib/, bin/slangc)")

find_program(CHEATAH_PLOT_SLANGC slangc
    HINTS "${CHEATAH_PLOT_VK_ROOT}/bin"
    DOC "Slang compiler (ships in the Vulkan SDK)")

if(CHEATAH_PLOT_SLANGC AND EXISTS "${CHEATAH_GPU_DIR}/gpu/dispatch/dispatch.hpp")
    # -- Compile the kernels: "entry|guard" per row; the guard keeps each compiled module to
    #    exactly one entry point and its own bindings.
    set(_shader_dir "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${_shader_dir}")
    set(_slang_src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/plot.slang")
    set(_shader_specs
        "plot_clear|KERNEL_plot_clear"
        "plot_raster|KERNEL_plot_raster")
    set(_shader_outputs "")
    foreach(_spec IN LISTS _shader_specs)
        string(REPLACE "|" ";" _parts "${_spec}")
        list(GET _parts 0 _op)
        list(GET _parts 1 _guard)
        set(_spv "${_shader_dir}/${_op}.spv")
        add_custom_command(OUTPUT "${_spv}"
            COMMAND "${CHEATAH_PLOT_SLANGC}" "${_slang_src}" -D${_guard}
                    -target spirv -entry ${_op} -stage compute -o "${_spv}"
            DEPENDS "${_slang_src}"
            COMMENT "slangc ${_op}.spv"
            VERBATIM)
        list(APPEND _shader_outputs "${_spv}")
        set(_metal_out "${_shader_dir}/${_op}.metal")
        add_custom_command(OUTPUT "${_metal_out}"
            COMMAND "${CHEATAH_PLOT_SLANGC}" "${_slang_src}" -D${_guard}
                    -target metal -entry ${_op} -stage compute -o "${_metal_out}"
            DEPENDS "${_slang_src}"
            COMMENT "slangc ${_op}.metal"
            VERBATIM)
        list(APPEND _shader_outputs "${_metal_out}")
    endforeach()
    add_custom_target(cheatah_plot_shaders ALL DEPENDS ${_shader_outputs})

    add_library(cheatah_plot_gpu INTERFACE)
    target_include_directories(cheatah_plot_gpu INTERFACE ${CHEATAH_GPU_DIR})

    # metal-cpp: Apple's MIT-licensed header-only C++ interface to Metal, vendored inside
    # cheatah-gpu (override with -DCHEATAH_PLOT_METAL_CPP or the CHEATAH_GPU_METAL_CPP env var).
    if(DEFINED ENV{CHEATAH_GPU_METAL_CPP})
        set(CHEATAH_PLOT_METAL_CPP "$ENV{CHEATAH_GPU_METAL_CPP}" CACHE PATH "metal-cpp headers")
    else()
        set(CHEATAH_PLOT_METAL_CPP "${CHEATAH_GPU_DIR}/third_party/metal-cpp" CACHE PATH "metal-cpp headers")
    endif()

    if(APPLE)
        # METAL is the mac lane: the real frameworks compile the slang-generated MSL at runtime.
        target_include_directories(cheatah_plot_gpu INTERFACE ${CHEATAH_PLOT_METAL_CPP})
        target_compile_definitions(cheatah_plot_gpu INTERFACE
            CHEATAH_PLOT_GPU_METAL=1
            CHEATAH_PLOT_MSL_DIR="${_shader_dir}")
        target_link_libraries(cheatah_plot_gpu INTERFACE
            "-framework Metal" "-framework Foundation" "-framework QuartzCore")
        list(APPEND CHEATAH_PLOT_GPU_LANES metal)
    else()
        # VULKAN lane: cheatah-gpu's vk.* forwarders over volk (preferred — the loader is
        # dlopen'd at runtime, no libvulkan DT_NEEDED) or the SDK headers + -lvulkan when no
        # volk source is around. Kernels load from the .spv directory baked in here (the
        # CHEATAH_PLOT_SPV_DIR environment variable overrides at runtime).
        file(GLOB _volk_candidates "${CHEATAH_GPU_DIR}/build/*/_deps/volk-src")
        list(SORT _volk_candidates)
        list(POP_BACK _volk_candidates _volk_default)
        set(CHEATAH_PLOT_VOLK_SRC "${_volk_default}" CACHE PATH "volk checkout (volk.h + volk.c)")
        if(EXISTS "${CHEATAH_PLOT_VOLK_SRC}/volk.c" OR CHEATAH_PLOT_VK_ROOT)
            target_compile_definitions(cheatah_plot_gpu INTERFACE
                CHEATAH_PLOT_GPU_VULKAN=1
                CHEATAH_PLOT_SPV_DIR="${_shader_dir}")
            if(CHEATAH_PLOT_VK_ROOT)
                target_include_directories(cheatah_plot_gpu INTERFACE
                    "${CHEATAH_PLOT_VK_ROOT}/include")
            endif()
            if(EXISTS "${CHEATAH_PLOT_VOLK_SRC}/volk.c")
                enable_language(C)
                add_library(cheatah_plot_volk STATIC "${CHEATAH_PLOT_VOLK_SRC}/volk.c")
                target_include_directories(cheatah_plot_volk PUBLIC "${CHEATAH_PLOT_VOLK_SRC}")
                if(CHEATAH_PLOT_VK_ROOT)
                    target_include_directories(cheatah_plot_volk PRIVATE
                        "${CHEATAH_PLOT_VK_ROOT}/include")
                endif()
                set_target_properties(cheatah_plot_volk PROPERTIES POSITION_INDEPENDENT_CODE ON)
                target_link_libraries(cheatah_plot_volk PUBLIC ${CMAKE_DL_LIBS})
                target_link_libraries(cheatah_plot_gpu INTERFACE cheatah_plot_volk)
                list(APPEND CHEATAH_PLOT_GPU_LANES "vulkan(volk)")
            else()
                target_link_directories(cheatah_plot_gpu INTERFACE "${CHEATAH_PLOT_VK_ROOT}/lib")
                target_link_libraries(cheatah_plot_gpu INTERFACE vulkan)
                list(APPEND CHEATAH_PLOT_GPU_LANES "vulkan(-lvulkan)")
            endif()
        endif()

        # EMULATED-METAL lane: cheatah-gpu's software Metal device + the raster stand-ins —
        # the bit-exact-by-construction mirror of raster_cpu, exercised on this Linux host.
        if(EXISTS "${CHEATAH_PLOT_METAL_CPP}/Metal" AND
           EXISTS "${CHEATAH_GPU_DIR}/gpu/metal/emulated/emulated_metal.cpp")
            target_include_directories(cheatah_plot_gpu INTERFACE
                ${CHEATAH_PLOT_METAL_CPP}
                ${CHEATAH_GPU_DIR}/gpu/metal/shim)
            target_compile_definitions(cheatah_plot_gpu INTERFACE CHEATAH_PLOT_GPU_METAL=1)
            # metal-cpp's documented calling convention needs blocks; cmath first keeps its
            # math macros out of the way (the same options cheatah-gpu's emulated lane uses).
            target_compile_options(cheatah_plot_gpu INTERFACE -fblocks -include cmath)
            target_sources(cheatah_plot_gpu INTERFACE
                ${CHEATAH_GPU_DIR}/gpu/metal/emulated/emulated_metal.cpp
                ${CHEATAH_GPU_DIR}/gpu/metal/emulated/block_stubs.cpp)
            list(APPEND CHEATAH_PLOT_GPU_LANES "metal(emulated)")
        endif()
    endif()

    if(CHEATAH_PLOT_GPU_LANES)
        message(STATUS "cheatah-plot: GPU lanes enabled: ${CHEATAH_PLOT_GPU_LANES} "
                       "(kernels -> ${_shader_dir})")
    else()
        message(STATUS "cheatah-plot: GPU lanes disabled (no usable device stack) — CPU path only")
    endif()
else()
    message(STATUS "cheatah-plot: GPU lanes disabled (needs slangc + a cheatah-gpu checkout) "
                   "— CPU path only")
endif()

# --- C++ unit tests: GoogleTest, fetched ONLY under this option --------------------------------
include(FetchContent)
if(CHEATAH_PLOT_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        52eb8108c5bdec04579160ae17225d66034bd723)  # pinned (matches ../cheatah)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Benchmarks: the renderer's hot paths under Google Benchmark (report-only, not gated) -------
option(CHEATAH_PLOT_BENCH "Build the Google Benchmark suite (fetches Google Benchmark)" OFF)
if(CHEATAH_PLOT_BENCH)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        192ef10025eb2c4cdd392bc502f0c852196baa48)  # pinned (matches ../cheatah)
    FetchContent_MakeAvailable(benchmark)
    add_executable(cheatah_plot_bench bench/plot_bench.cpp)
    target_link_libraries(cheatah_plot_bench PRIVATE
        cheatah::plot cheatah_math cheatah_statistics cheatah_linalg benchmark::benchmark)
    target_include_directories(cheatah_plot_bench PRIVATE ${CHEATAH_DIR}/stdlib/ndarray)
    target_compile_options(cheatah_plot_bench PRIVATE -O2)
endif()

# --- The cheatah (.purr) system suite, run through the qa_gate as one ctest ---------------------
include(CTest)
if(BUILD_TESTING)
    add_test(NAME qa_gate
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/qa_gate.sh
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(qa_gate PROPERTIES
        ENVIRONMENT "CHEATAH_DIR=${CHEATAH_DIR};CHEATAH_GPU_DIR=${CHEATAH_GPU_DIR}")
endif()
