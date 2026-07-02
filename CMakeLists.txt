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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
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

# --- The cheatah (.purr) system suite, run through the qa_gate as one ctest ---------------------
include(CTest)
if(BUILD_TESTING)
    add_test(NAME qa_gate
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/qa_gate.sh
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(qa_gate PROPERTIES
        ENVIRONMENT "CHEATAH_DIR=${CHEATAH_DIR};CHEATAH_GPU_DIR=${CHEATAH_GPU_DIR}")
endif()
