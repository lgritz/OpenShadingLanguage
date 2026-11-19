<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- Copyright Contributors to the Open Shading Language Project. -->

# Fuzzing OSL

OSL uses [libFuzzer](https://llvm.org/docs/LibFuzzer.html) to exercise its
input readers against malformed data. A single binary, `osl_fuzz`, covers
every fuzz *target* (kind of input) by dispatching at runtime based on the
`OSL_FUZZ_TARGET` environment variable. Nightly CI fuzzes each target in
parallel; crash reproducers are uploaded as GitHub Actions artifacts.

This setup deliberately mirrors OpenImageIO's (`docs/dev/fuzzing.md` in the
OIIO repo), with "target" in OSL playing the role that "image format" plays in
OIIO. Keep the two analogous when changing either.


## Targets

| Target | Input | What each input goes through |
|--------|-------|------------------------------|
| `oso`  | Compiled shader bytecode (`.oso`) | `ShadingSystem::LoadMemoryCompiledShader`, then a one-layer shader group is built from it and run through the runtime optimizer (`optimize_group` with JIT disabled). |

Planned but not yet implemented: `osl` (shader source through the
compiler) and `scene` (testrender scene files). See
"Adding a new target" below.

Native code generation (LLVM JIT) is intentionally not exercised: it is slow
per input, and LLVM is a prebuilt, uninstrumented dependency, so the fuzzer
would get little coverage feedback from it.


## Prerequisites

- **clang >= 14** with libFuzzer support (`-fsanitize=fuzzer`). GCC does not
  support libFuzzer; CMake warns and skips the fuzz target.
  - On **macOS**, Apple's clang (from Xcode / Command Line Tools) does *not*
    ship the libFuzzer runtime, so `-fsanitize=fuzzer` fails to link. Install
    upstream LLVM (`brew install llvm`) and point CMake at it:
    `-DCMAKE_C_COMPILER=$(brew --prefix llvm)/bin/clang
    -DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++`. With AppleClang,
    CMake warns and skips the fuzz target (the rest of OSL still builds).
- All of OSL's usual build dependencies.


## Building the fuzz target

```bash
cmake -B build -S . \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Release \
    -DOSL_BUILD_FUZZ_TARGETS=ON \
    -DSANITIZE=address,undefined \
    -DOSL_BUILD_TESTS=OFF -DUSE_PYTHON=OFF

cmake --build build --target install
```

`OSL_BUILD_FUZZ_TARGETS=ON` is the only new flag (default OFF; it can also be
set as an environment variable). `SANITIZE=address,undefined` instruments the
OSL libraries for maximum bug detection, and with clang it also adds
`-fsanitize=fuzzer-no-link` coverage instrumentation to the libraries so
libFuzzer can steer into the loader and optimizer. Omitting it produces a
valid binary, but one that is effectively coverage-blind. The harness itself
is always built with ASan and UBSan.

`OSL_BUILD_TESTS=OFF` and `USE_PYTHON=OFF` just speed up the build; `oslc` is
always built, and is needed to generate seeds.

By default the local fuzzing engine is `-fsanitize=fuzzer`. On OSS-Fuzz the
`LIB_FUZZING_ENGINE` environment variable is set to the engine's `.a` path
and is picked up automatically by `src/fuzz/CMakeLists.txt`.

The binary is installed as `<prefix>/bin/osl_fuzz`, next to
`osl_fuzz.options` (default libFuzzer options, as used by OSS-Fuzz).


## Listing targets

```bash
osl_fuzz --list-targets
```

Prints one target name per line. This list drives the CI lint check: the
build fails if any target returned here lacks a directory under
`src/fuzz/corpora/`.


## Gathering seeds

Random bytes almost never get past the `.oso` header, so start from real
shaders. `src/fuzz/populate_corpora.py` gathers seeds for a target:

```bash
python3 src/fuzz/populate_corpora.py --target oso --dest /tmp/seeds \
    --oslc <prefix>/bin/oslc
```

For `oso` it copies every committed `.oso` under `testsuite/` (hand-written
loader regression fixtures) and compiles every `.osl` under `testsuite/` and
`src/shaders/` with `oslc` (about 1400 shaders). A couple of dozen testsuite
shaders are deliberately invalid and fail to compile; that is expected.
There is a per-file size cap (1 MB) but no cap on the number of seeds;
`osl_fuzz -merge=1` (run by CI) distills them to the coverage-increasing
subset.

Without `--dest`, seeds go into `src/fuzz/corpora/<target>/`, which in git
holds only a `.gitkeep`. Do not commit generated seeds.


## Running the fuzzer locally

```bash
mkdir -p /tmp/corpus
# Fuzz .oso loading for 60 seconds, starting from the gathered seeds:
OSL_FUZZ_TARGET=oso \
    <prefix>/bin/osl_fuzz /tmp/corpus /tmp/seeds/oso \
    -max_total_time=60 -max_len=1048576

# Useful flags:
#   -timeout=60       kill any single input that takes longer than 60 s
#   -jobs=4           run 4 worker processes in parallel
#   -runs=0           process seeds only, no mutation (useful to verify seeds)
#   -rss_limit_mb=4096 -malloc_limit_mb=2048   the limits CI uses
```

The fuzzer writes new interesting inputs into the first corpus directory and
any crash reproducers as `crash-<hash>` files in the current directory.

OSL's own error messages for rejected inputs are discarded during fuzzing
for speed. Set `OSL_FUZZ_VERBOSE=1` to see them, which is handy when looking
at a single input. (`OSL_ASSERT` failures always print.)


## Reproducing a CI crash

1. Download the crash artifact from the failed GitHub Actions run (named
   `fuzz-crashes-<target>-<run_id>`).
2. Extract the `crash_<target>_<hash>` file.
3. Run:

```bash
OSL_FUZZ_TARGET=<target> OSL_FUZZ_VERBOSE=1 \
    <prefix>/bin/osl_fuzz crash_<target>_<hash>
```

The AddressSanitizer or UBSan report appears on stderr. The key fields are
the error type (e.g. `heap-buffer-overflow`), the stack trace at the point of
the violation, and the allocation site. For the `oso` target the input is
text, so it can be read directly.


## Minimizing a crash

```bash
OSL_FUZZ_TARGET=<target> \
    <prefix>/bin/osl_fuzz \
    -minimize_crash=1 \
    -exact_artifact_path=min_crash \
    crash_<target>_<hash>
```

For `.oso` inputs, hand-editing the reproducer down to a small, readable
shader is often better still.


## Turning a finding into a regression test

Fix the bug in a separate PR, and add a testsuite case containing the
minimized input so it can never come back. For the `oso` target, follow
`testsuite/oso-excess-defaults/`: a `testsuite/oso-<name>/` directory with the
`.oso` file(s), a `run.py` that loads them (e.g. with `testshade`), and a
reference output showing the input is rejected with an error. Committed
`.oso` files under `testsuite/` are automatically picked up as seeds by
`populate_corpora.py`, so there is no need to also add them to
`src/fuzz/corpora/`.


## How target selection works

`osl_fuzz` resolves the active target in priority order:

1. `OSL_FUZZ_TARGET` environment variable -- used by CI matrix jobs.
2. `basename(argv[0])` stripped of a `fuzz_` prefix -- for OSS-Fuzz-style
   per-target symlinks (`fuzz_oso -> osl_fuzz`).
3. `--target=<name>` command-line argument.
4. None set: the binary prints the available targets and exits with an
   error.


## Adding a new target

1. Write `src/fuzz/fuzz_<name>.cpp` implementing `<name>_init()` and
   `<name>_run()`; declare them in `src/fuzz/fuzz_targets.h`; add a row to the
   table in `src/fuzz/osl_fuzz.cpp`; add the source (and any extra library,
   e.g. `oslcomp`) in `src/fuzz/CMakeLists.txt`.
2. Create `src/fuzz/corpora/<name>/.gitkeep`. The CI lint job
   (`fuzz-corpus-lint` in `.github/workflows/fuzz.yml`) fails until it
   exists.
3. Add seed sources for the target to `TARGET_SOURCES` in
   `src/fuzz/populate_corpora.py`.
4. Add a matrix line to `.github/workflows/fuzz.yml`.
5. Add the target to the table at the top of this document.

The `<name>_run()` function must return normally for any input. If the code
being fuzzed calls `exit()` on bad input (as testrender's scene reader
currently does), that has to be changed to an error return first.


## Memory limits and false-positive OOMs

libFuzzer enforces an `rss_limit_mb` (4096 MB) and reports a crash if the
process exceeds it; CI also passes `-malloc_limit_mb=2048`
(`src/build-scripts/ci-fuzztest.bash`), which reports any single allocation
over 2 GB, and `-max_len=1048576` to keep inputs at most 1 MB.

Unlike OIIO, OSL has no configurable resource limits to lower for fuzzing
(there is no analog of `limits:imagesize_MB`). An input that declares an
absurd size (for example, an enormous array length) and makes OSL allocate
past the malloc limit is therefore a real finding: the loader should reject
it with a sanity check, as was done for excess initializers in #2220.

One expected source of slow memory growth: OSL interns every identifier and
string it reads as a `ustring`, and those are never freed. Over a long run
the fuzzer's novel strings make RSS climb gradually. If an RSS-limit OOM is
reported after a long run and the top allocations are in the ustring table,
that is a harness artifact, not an OSL bug.

Per-iteration shader state does not accumulate: every input is loaded under
the same shader name with `allow_shader_replacement` enabled, so each load
replaces the previous one.


## CI overview

- **Fuzz** (`.github/workflows/fuzz.yml`): one parallel job per target,
  nightly on the main repo, on pushes and PRs from branches whose name
  contains `fuzz`, and on manual dispatch (optionally for a single target).
  Each job builds with clang + ASan + UBSan, gathers seeds, merges them into
  the evolved corpus restored from the GitHub Actions cache (keyed per target
  per branch), fuzzes for the target's time budget, saves the corpus back to
  the cache, and on failure uploads reproducers. Logic lives in
  `src/build-scripts/ci-fuzztest.bash`.
- **Corpus lint** (`fuzz-corpus-lint` job in the same workflow): builds
  `osl_fuzz`, runs `--list-targets`, and fails if any target lacks a
  `src/fuzz/corpora/<target>/` directory.

Time budgets per target are declared in the workflow matrix: `oso` gets 60
minutes (the analog of OIIO's "tier 1").


## OSS-Fuzz

Not yet onboarded (same as OIIO). The harness already supports what OSS-Fuzz
needs (`$LIB_FUZZING_ENGINE` linkage, `argv[0]`-based per-target dispatch for
`fuzz_<target>` symlinks, `--list-targets`, `osl_fuzz.options`), so
onboarding should be a small, additive step.
