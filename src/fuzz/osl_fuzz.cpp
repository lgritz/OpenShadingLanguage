// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

// osl_fuzz: a single libFuzzer harness binary covering every OSL fuzz
// target (input type). It is modeled on OpenImageIO's oiio_fuzz_image, with
// "target" playing the role that "format" plays there; see
// docs/dev/fuzzing.md.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>

#include "fuzz_targets.h"

using OIIO::Strutil::starts_with;
using OSL::print;


// Table of all fuzz targets compiled into this harness. Add new targets here
// (see fuzz_targets.h for the full checklist).
static const FuzzTarget fuzz_targets[] = {
    { "oso", oso_init, oso_run },
};


// Target selected for this process, set once in LLVMFuzzerInitialize.
static const FuzzTarget* active_target = nullptr;



static const FuzzTarget*
find_target(OSL::string_view name)
{
    for (auto& t : fuzz_targets)
        if (name == t.name)
            return &t;
    return nullptr;
}



// Return all target names, sorted.
static std::vector<std::string>
all_targets()
{
    std::vector<std::string> result;
    for (auto& t : fuzz_targets)
        result.emplace_back(t.name);
    std::sort(result.begin(), result.end());
    return result;
}



extern "C" int
LLVMFuzzerInitialize(int* argc, char*** argv)
{
    std::string target;

    // Priority 1: OSL_FUZZ_TARGET env var (used by GHA matrix jobs).
    if (const char* env = getenv("OSL_FUZZ_TARGET"))
        target = env;

    // Priority 2: argv[0] basename stripped of "fuzz_" prefix.
    // OSS-Fuzz invokes per-target symlinks: fuzz_oso -> osl_fuzz.
    if (target.empty()) {
        std::string base = OIIO::Filesystem::filename((*argv)[0]);
        if (starts_with(base, "fuzz_"))
            target = base.substr(5);
    }

    // Priority 3: --target=<name> pseudo-arg. Also handle --list-targets.
    // Strip our pseudo-args before returning so libFuzzer doesn't reject them.
    for (int i = 1; i < *argc;) {
        OSL::string_view arg((*argv)[i]);
        if (arg == "--list-targets") {
            for (auto& t : all_targets())
                print("{}\n", t);
            // Flush before exit(): ASan's LSAN atexit hook calls _Exit() when
            // it finds leaks, skipping stdio auto-flush. Flushing first
            // ensures the list is on the pipe regardless of the atexit order.
            fflush(stdout);
            exit(0);
        }
        if (starts_with(arg, "--target=")) {
            if (target.empty())
                target = std::string(arg.substr(9));
            for (int j = i; j < *argc - 1; ++j)
                (*argv)[j] = (*argv)[j + 1];
            --*argc;
            continue;
        }
        ++i;
    }

    if (target.empty()) {
        print(stderr, "osl_fuzz: no target specified.\n"
                      "  Set OSL_FUZZ_TARGET=<target>, use --target=<name>, "
                      "or invoke as fuzz_<target>.\n"
                      "  Available targets:\n");
        for (auto& t : all_targets())
            print(stderr, "    {}\n", t);
        exit(1);
    }

    active_target = find_target(target);
    if (!active_target) {
        print(stderr,
              "osl_fuzz: unknown target '{}'\n"
              "  Available targets:\n",
              target);
        for (auto& t : all_targets())
            print(stderr, "    {}\n", t);
        exit(1);
    }

    // Keep OIIO (whose TextureSystem the ShadingSystem creates) from
    // spawning worker threads; fuzzing is single-threaded per process.
    OIIO::attribute("threads", 1);

    active_target->init();
    return 0;
}



extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    active_target->run(OSL::cspan<uint8_t>(data, size));
    return 0;
}
