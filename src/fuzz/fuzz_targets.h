// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <cstdint>

#include <OSL/oslconfig.h>


// A fuzz target is one kind of OSL input that the osl_fuzz harness can be
// pointed at (the OSL analog of an image format in OpenImageIO's
// oiio_fuzz_image). One target is selected per process; see osl_fuzz.cpp.
//
// To add a target (e.g. "osl" for shader source, "scene" for testrender
// scene files):
//   1. Write fuzz_<name>.cpp implementing <name>_init() and <name>_run().
//   2. Declare them below.
//   3. Add a row to the target table in osl_fuzz.cpp.
//   4. Add the source file to src/fuzz/CMakeLists.txt.
//   5. Add src/fuzz/corpora/<name>/, seed sources in populate_corpora.py,
//      and a matrix line in .github/workflows/fuzz.yml.
struct FuzzTarget {
    // Lowercase name, used for OSL_FUZZ_TARGET, fuzz_<name> symlinks,
    // --target=<name>, the corpus directory, and CI job/artifact names.
    const char* name;
    // Called once, after target selection, before any input is processed.
    void (*init)();
    // Called for every input. Must return normally for any input; anything
    // else (crash, sanitizer report, abort, hang) is a finding.
    void (*run)(OSL::cspan<uint8_t> data);
};


// "oso" target: compiled shader bytecode (fuzz_oso.cpp).
void
oso_init();
void
oso_run(OSL::cspan<uint8_t> data);
