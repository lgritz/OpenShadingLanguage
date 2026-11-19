# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# This file is only reached when OSL_BUILD_FUZZ_TARGETS=ON (guarded in the
# root CMakeLists.txt). It is modeled on OpenImageIO's src/fuzz/CMakeLists.txt
# and the two should be kept analogous.

# libFuzzer requires a capable clang. If the compiler can't build the fuzz
# targets, skip them with a warning rather than failing the whole configure --
# that lets the rest of OSL still build in a tree that happens to have
# OSL_BUILD_FUZZ_TARGETS=ON with an unsuitable compiler.
#
# - GCC does not support libFuzzer at all.
# - Apple's clang (CMAKE_CXX_COMPILER_ID "AppleClang", shipped with Xcode and
#   the Command Line Tools) does not ship the libFuzzer runtime
#   (libclang_rt.fuzzer_osx.a), so -fsanitize=fuzzer fails to link. Use an
#   upstream LLVM clang instead (e.g. `brew install llvm`).
if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message (WARNING
        "OSL_BUILD_FUZZ_TARGETS=ON requires clang; skipping fuzz targets "
        "(detected ${CMAKE_CXX_COMPILER_ID}). Set "
        "CMAKE_CXX_COMPILER=clang++ to build them.")
    return ()
endif ()
if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    message (WARNING
        "OSL_BUILD_FUZZ_TARGETS=ON does not work with Apple's clang, which "
        "lacks the libFuzzer runtime; skipping fuzz targets. Install upstream "
        "LLVM (e.g. 'brew install llvm') and configure with "
        "-DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++ "
        "-DCMAKE_C_COMPILER=$(brew --prefix llvm)/bin/clang.")
    return ()
endif ()

# The fuzz targets are always instrumented with the address sanitizer (see the
# target_compile_options below), independently of the global SANITIZE option.
# _FORTIFY_SOURCE is incompatible with ASan (clang predefines it to 0), so strip
# any inherited _FORTIFY_SOURCE define for this directory to avoid a
# -Wmacro-redefined error when OSL's hardening defined it.
get_directory_property (_fuzz_defs COMPILE_DEFINITIONS)
list (FILTER _fuzz_defs EXCLUDE REGEX "^_FORTIFY_SOURCE")
set_directory_properties (PROPERTIES COMPILE_DEFINITIONS "${_fuzz_defs}")

# Resolve the fuzzing engine.
# - Local dev: defaults to -fsanitize=fuzzer (links the libFuzzer runtime).
# - OSS-Fuzz: sets LIB_FUZZING_ENGINE to a .a path for the chosen engine.
if (DEFINED ENV{LIB_FUZZING_ENGINE})
    set (OSL_FUZZING_ENGINE "$ENV{LIB_FUZZING_ENGINE}")
else ()
    set (OSL_FUZZING_ENGINE "-fsanitize=fuzzer")
endif ()

# One harness binary for all fuzz targets (input types). The executable is
# named osl_fuzz (not just fuzz) so that if it is ever accidentally built and
# installed, the name makes clear which package it belongs to.
#
# osl_fuzz.cpp holds the libFuzzer entry points and target dispatch; each
# target lives in its own fuzz_<target>.cpp. To add a target, add its source
# file here (and link any extra library it needs, e.g. oslcomp for "osl").
add_executable (osl_fuzz
    osl_fuzz.cpp
    fuzz_oso.cpp
)

target_include_directories (osl_fuzz BEFORE PRIVATE ${OpenImageIO_INCLUDES})

# OSL builds with hidden symbol visibility by default, but libFuzzer finds the
# optional LLVMFuzzerInitialize hook by dynamic symbol lookup on some
# platforms (e.g. macOS), so the harness's entry points must stay visible.
set_target_properties (osl_fuzz PROPERTIES
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF)

target_link_libraries (osl_fuzz PRIVATE oslexec)

# -fsanitize=fuzzer-no-link instruments code for coverage tracing without
# linking the fuzzer runtime; the runtime comes from OSL_FUZZING_ENGINE at
# link time. address+undefined catch memory errors and UB respectively.
target_compile_options (osl_fuzz PRIVATE
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-omit-frame-pointer
)

target_link_options (osl_fuzz PRIVATE
    ${OSL_FUZZING_ENGINE}
    -fsanitize=address,undefined
)

install_targets (osl_fuzz)

# Install the libFuzzer options file alongside the binary. OSS-Fuzz/ClusterFuzz
# keys the options file to the binary basename, so it is named to match:
# osl_fuzz.options.
install (FILES osl_fuzz.options DESTINATION ${CMAKE_INSTALL_BINDIR})
