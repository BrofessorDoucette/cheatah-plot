# C++ unit tests for the plot package's generated headers.
#
# The single binary `cheatah_plot_tests` is what scripts/coverage.sh instruments (clang source-based
# coverage) and what security/run-valgrind.sh memchecks. Linking the cheatah::plot INTERFACE target puts
# the repo root on the include path so `#include "plot/scale/scale.hpp"` resolves.
#
# DIVERGENCE from cheatah-gpu: its seed header (gpu/dispatch/dispatch.hpp) was pure inline C++ with zero
# dependencies, so its test just linked GoogleTest. cheatah-plot's generated headers are authored in
# cheatah and lean on the cheatah STANDARD LIBRARY — scale.hpp `#include`s cheatah.hpp/math.hpp/
# ndarray.hpp AND calls out-of-line symbols (builtins::to_float, math::log10/floor). ndarray is
# header-only for what plot.scale uses (only its include dir is needed), but `builtins` and `math` have
# compiled sources.
#
# We bring those in the standard way a C++ program consumes a cheatah stdlib module: COMPILE the module
# source into our own STATIC target from the sibling ../cheatah checkout (CHEATAH_DIR), rather than
# hunting for a prebuilt libcheatah_<mod>.a in some build/ tree. Building from source means the stdlib is
# compiled with THIS build's compiler and flags — so the ASan preset sanitizes it and the coverage build
# instruments it (no archive/config mismatch), and the tests need only a cheatah SOURCE checkout, not a
# pre-built one. Each module's directory is its own public include dir.
#
# (Consumers of cheatah-plot itself never do any of this: they `biome add cheatah-plot` and `import plot`
# — biome wires cheatah-gpu, CHEATAH_MODULE_PATH, and the stdlib. This is only cheatah-plot's own
# dev-time C++ coverage harness for the generated headers; see scripts/test-biome-install.sh for the
# real consumer path.)
include(GoogleTest)

set(_cheatah_stdlib ${CHEATAH_DIR}/stdlib)
if(NOT EXISTS ${_cheatah_stdlib}/math/math.cpp)
    message(FATAL_ERROR
        "cheatah-plot tests: ${_cheatah_stdlib} is not a cheatah checkout — set CHEATAH_DIR to your "
        "cheatah source tree (default ../cheatah).")
endif()

# The stdlib modules whose compiled symbols plot.scale calls. `math` builds on `builtins`, so it PUBLIC-
# links it (propagating both the include dir and the correct static-link order). Guarded so a future
# consumer target in this repo can reuse them without redefinition.
foreach(_mod builtins math statistics)
    if(NOT TARGET cheatah_${_mod})
        add_library(cheatah_${_mod} STATIC ${_cheatah_stdlib}/${_mod}/${_mod}.cpp)
        target_include_directories(cheatah_${_mod} PUBLIC ${_cheatah_stdlib}/${_mod})
        target_compile_features(cheatah_${_mod} PUBLIC cxx_std_20)
        set_target_properties(cheatah_${_mod} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endforeach()
target_link_libraries(cheatah_math PUBLIC cheatah_builtins)
target_link_libraries(cheatah_statistics PUBLIC cheatah_math)

# linalg's sources are routines.cpp + simd.cpp (no single-file convention), and it reads the
# header-only ndarray surface — plot.stats calls its lstsq for the fit line.
if(NOT TARGET cheatah_linalg)
    add_library(cheatah_linalg STATIC
        ${_cheatah_stdlib}/linalg/routines.cpp
        ${_cheatah_stdlib}/linalg/simd.cpp
    )
    target_include_directories(cheatah_linalg PUBLIC
        ${_cheatah_stdlib}/linalg
        ${_cheatah_stdlib}/ndarray
    )
    target_compile_features(cheatah_linalg PUBLIC cxx_std_20)
    set_target_properties(cheatah_linalg PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(cheatah_linalg PUBLIC cheatah_math)
endif()

add_executable(cheatah_plot_tests
    scale_test.cpp
    color_test.cpp
    series_test.cpp
    stats_test.cpp
    figure_test.cpp
    raster_test.cpp
    png_test.cpp
    font_test.cpp
    render_test.cpp
    gpu_raster_test.cpp
)
target_include_directories(cheatah_plot_tests PRIVATE ${_cheatah_stdlib}/ndarray)  # header-only ndarray surface
target_link_libraries(cheatah_plot_tests PRIVATE
    cheatah::plot
    cheatah_math          # brings cheatah_builtins transitively
    cheatah_statistics
    cheatah_linalg
    GTest::gtest_main
)

# The GPU lanes, when the top-level configure enabled them: the lane defines + device stacks ride
# the one interface target, and the kernels must be compiled before the binary runs them. Without
# the target the same test file builds CPU-only (its GPU cases skip).
if(TARGET cheatah_plot_gpu)
    target_link_libraries(cheatah_plot_tests PRIVATE cheatah_plot_gpu)
    add_dependencies(cheatah_plot_tests cheatah_plot_shaders)
endif()

# Optional ASan + UBSan (the `asan` preset turns this on). Sanitize the stdlib we compiled too, so the
# whole binary is instrumented consistently. Valgrind runs the plain debug build.
if(CHEATAH_PLOT_SANITIZE)
    foreach(_t cheatah_plot_tests cheatah_builtins cheatah_math cheatah_statistics cheatah_linalg)
        target_compile_options(${_t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endforeach()
    target_link_options(cheatah_plot_tests PRIVATE -fsanitize=address,undefined)
    if(TARGET cheatah_plot_gpu)
        # metal-cpp deliberately calls objc_msgSend through a typed function-pointer cast — its
        # documented mechanism — so the `function` UBSan check is off on the GPU-enabled binary,
        # matching cheatah-gpu's own sanitizer gate.
        target_compile_options(cheatah_plot_tests PRIVATE -fno-sanitize=function)
    endif()
endif()

gtest_discover_tests(cheatah_plot_tests)
