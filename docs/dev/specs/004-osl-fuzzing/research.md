# Research: OSL Input Fuzzing Infrastructure

Reference implementation throughout: OpenImageIO's fuzzing
(`src/fuzz/`, `src/build-scripts/ci-fuzztest.bash`, `.github/workflows/fuzz.yml`,
`docs/dev/fuzzing.md`, spec `docs/dev/specs/001-image-fuzzing/`). "OIIO does X"
below refers to that code as of OIIO main, 2026-11.

## R1. Harness shape: one binary, target dispatch

- **Decision**: One binary `osl_fuzz`, dispatching over a small static table
  of targets `{name, init(), run(data,size)}`. This feature registers only
  `oso`. Selection priority identical to OIIO: `OSL_FUZZ_TARGET` env var, then
  `argv[0]` basename `fuzz_<target>`, then `--target=<name>` (stripped from argv
  before libFuzzer sees it). `--list-targets` prints registered targets and
  exits 0. No target -> print available targets, exit 1.
- **Rationale**: Mirrors `oiio_fuzz_image` exactly, with "target" in place of
  "format". A static table makes adding `osl`/`scene` a matter of one new
  source file and one table row (SC-010). OIIO discovers formats from
  `get_extension_map()`; OSL has no such registry, so the table is the
  registry.
- **Alternatives**: One binary per target (OIIO's original plan, abandoned
  there for the single-binary design -- rejected for the same reasons).
  `--format=` spelling -- rejected, "format" is wrong for OSL.
- **Naming**: binary `osl_fuzz` (OSL prefix per FR-020; analog of
  `oiio_fuzz_image`, without a domain suffix because OSL's binary spans
  domains). Source split: `src/fuzz/osl_fuzz.cpp` (init, dispatch, libFuzzer
  entry points) and `src/fuzz/fuzz_oso.cpp` (oso target). Future:
  `fuzz_osl.cpp`, `fuzz_scene.cpp`. OIIO's `fuzz_image.cpp` is a single file
  because it has one code path for all formats; OSL's targets share nothing
  but setup, so one file per target reads better.

## R2. What the `oso` target does per input

- **Decision** (clarified in spec): 
  1. `ss->LoadMemoryCompiledShader("fuzz", bytes)`; on failure return.
  2. `ShaderGroupBegin("fuzz")`, `Shader(*group, "surface", "fuzz", "layer")`,
     `ShaderGroupEnd(*group)`; on any failure return.
  3. `ss->optimize_group(group.get(), nullptr, /*do_jit=*/false)`.
  4. Drop the `ShaderGroupRef`.
- **Rationale**: Covers lexer/parser (`osolex.l`, `osogram.y`,
  `OSOReaderToMaster`), `ShaderMaster::resolve_syms`, instance creation, and
  the runtime optimizer (constant folding, dead-code elimination,
  coalescing), which is most of OSL's own code that consumes `.oso` data.
  LLVM codegen is skipped (clarification Q1).
- Verified: `optimize_group(group, ctx=nullptr, ...)` allocates its own
  context (`shadingsys.cpp` ~4072). Shader usage string is free-form; the
  first `Shader()` call sets the group usage, so `"surface"` is fine for any
  shader type.
- **Alternatives**: load only (too shallow); with JIT (slow, uninstrumented
  LLVM).

## R3. Per-iteration state must not accumulate

- **Finding**: `LoadMemoryCompiledShader` stores masters in
  `m_shader_masters` keyed by name and refuses to replace an existing name
  unless the `allow_shader_replacement` attribute is set
  (`loadshader.cpp:667`).
- **Decision**: Set `allow_shader_replacement=1` at init and always load under
  the fixed name `"fuzz"`. Each new load replaces the map entry; the previous
  master is freed when the iteration's group ref is dropped. No ShadingSystem
  re-creation per input (too slow; would also re-init LLVM).
- **Residual growth**: every identifier/string literal is interned as a
  `ustring` forever. Mitigations: (a) cap `-max_len` at 1 MiB (real `.oso`
  files are tens of KB; largest shader source in repo ~13 KB); (b) keep
  libFuzzer's `rss_limit_mb=4096` as in OIIO; (c) document in
  `docs/dev/fuzzing.md` that a slow RSS climb is expected, and that an OOM
  whose top allocations are in the ustring table is a harness artifact, not a
  bug. If it proves a problem in practice, add `-max_total_time` restarts
  (libFuzzer `-jobs` already restarts workers). No engineering beyond that now.

## R4. Assertions

- **Finding**: `OSL_ASSERT` prints and **continues** in release builds
  (`OSL_ABORT_IF_DEBUG` is `abort()` only when `!NDEBUG`;
  `src/include/OSL/platform.h:478`). `OSL_DASSERT` compiles away in release.
  OIIO's fuzz CI runs Release with the same pattern for `OIIO_ASSERT`.
- **Decision**: Keep Release (throughput, matches OIIO). Do not change assert
  semantics in this feature. The harness checks nothing extra; a failed
  `OSL_ASSERT` that leads to memory corruption is caught by ASan, and one that
  doesn't is visible in the log but not a failure.
- **Noted for later (not in scope)**: a fuzz-only define that makes
  `OSL_ASSERT` abort would turn every assertion into a finding. Would need the
  same change in OIIO to stay analogous; raise with the user rather than do
  unilaterally.

## R5. ShadingSystem setup for fuzzing (`OSL_FUZZ_INIT` analog)

- **Finding**: The ShadingSystem constructor dereferences the renderer
  (`renderer->batched(...)`, `shadingsys.cpp` ~1110), so it needs a real
  `RendererServices`. The base class's defaults suffice for optimization.
- **Decision**: In `LLVMFuzzerInitialize`, once:
  - `OIIO::attribute("threads", 1)` (OIIO's texture system is created
    implicitly; keep it single-threaded as OIIO's harness does).
  - Construct a static `RendererServices`, a quiet `ErrorHandler` subclass,
    and a static `ShadingSystem(&rs, nullptr, &errhandler)`.
  - `ss->attribute("allow_shader_replacement", 1)` (R3).
  - `ss->attribute("lockgeom", 1)` (default; explicit for determinism).
  - No OSL-side memory-limit attributes exist analogous to OIIO's
    `limits:imagesize_MB`. Oversized declarations (huge array lengths) that
    allocate past `-malloc_limit_mb` are **findings** to be fixed in the
    loader with sanity bounds (separate PRs with regression tests, like
    #2220), not something to configure around.
- **Quiet error handler**: overrides `operator()` to discard all messages
  unless env `OSL_FUZZ_VERBOSE` is set, so reproducing one input shows OSL's
  diagnostics (FR-008). Applies to `ShadingSystem` errors only; `OSL_ASSERT`
  prints directly to stderr regardless (rare, and useful).

## R6. Build integration

- **Decision**: Mirror OIIO line for line, adjusted for OSL's cmake:
  - `src/cmake/compiler.cmake`, next to `set_cache (SANITIZE ...)`:
    `set_option (${PROJ_NAME}_BUILD_FUZZ_TARGETS "Build libFuzzer fuzz targets (requires clang)" OFF)`,
    and inside the `if (SANITIZE ...)` block, when fuzz targets are on and the
    compiler is clang, `add_compile_options (-fsanitize=fuzzer-no-link)` so
    the OSL libraries get coverage instrumentation (OIIO #5586).
  - Root `CMakeLists.txt`: `if (OSL_BUILD_FUZZ_TARGETS) add_subdirectory (src/fuzz) endif ()`.
  - `src/fuzz/CMakeLists.txt`: same compiler gating (warn+return for
    non-clang and AppleClang), same `_FORTIFY_SOURCE` stripping, same
    `LIB_FUZZING_ENGINE` resolution, `-fsanitize=fuzzer-no-link,address,undefined`
    on the harness, link `oslexec` (+ `oslcomp` later for the `osl` target),
    `install_targets (osl_fuzz)`, install `osl_fuzz.options` to bin.
- **Divergence found during implementation**: OSL compiles with
  `-fvisibility=hidden`, which hides `LLVMFuzzerInitialize`; on macOS
  libFuzzer finds that optional hook by dynamic lookup, so it was silently
  never called. `src/fuzz/CMakeLists.txt` sets
  `CXX_VISIBILITY_PRESET default` on `osl_fuzz`. (OIIO apparently does not
  need this; worth checking there on macOS.)
- `set_option` -> `set_cache` -> reads an env var of the same name, so CI can
  pass `OSL_BUILD_FUZZ_TARGETS=ON` and `SANITIZE=address,undefined` via
  `setenvs`, as OIIO does.
- OSL's existing `_FORTIFY_SOURCE` logic already skips when
  `SANITIZE` contains address; the per-directory strip is still needed for a
  fuzz build without global `SANITIZE` (harness always uses ASan).

## R7. Seeds

- **Finding**: Only 9 `.oso` files are committed (all hand-written loader
  regression fixtures: `testsuite/oso-excess-defaults/`,
  `testsuite/unknown-instruction/`). ~1440 `.osl` files exist under
  `testsuite/` and `src/shaders/`; 414 testsuite shaders `#include` local
  headers. OSL has no companion sample-file repo (OIIO has `oiio-images`).
- **Decision**: `src/fuzz/populate_corpora.py`, same CLI as OIIO's
  (`--target NAME`, `--dest DIR`, `--dry-run`; `--target` replaces
  `--format`). `TARGET_SOURCES` maps target -> list of source specs. For
  `oso`:
  - copy every committed `*.oso` under `testsuite/`;
  - compile every `*.osl` under `testsuite/` and `src/shaders/` with the
    installed `oslc` (located via `--oslc PATH`, else `$OSL_ROOT/bin/oslc`,
    else `dist/bin/oslc`, else `build/bin/oslc`), with `-I<shader's dir>`,
    output to `<dest>/oso/<unique-name>.oso`. Compile failures are expected
    for some deliberately-bad testsuite shaders and are counted, not fatal.
  - Per-file cap `MAX_BYTES = 1 MiB`; no count cap. Minimization happens in
    `ci-fuzztest.bash` via `-merge=1`, as in OIIO.
  - Unique output names: `<relative-dir-with-slashes-as-underscores>_<stem>.oso`
    (many files are named `test.osl`).
  - Parallel compilation via `concurrent.futures` to keep wall time low.
- `src/fuzz/corpora/oso/.gitkeep` is the only committed corpus content; no
  synthetic seeds needed since the testsuite provides plenty.
- Future `osl` target: same `.osl` list copied directly. Future `scene`:
  `testsuite/**/*.xml`. Each is one new `TARGET_SOURCES` entry.

## R8. CI

- **Decision**: Mirror OIIO's structure:
  - `.github/workflows/fuzz.yml`: triggers (nightly cron on canonical repo
    only, push/PR on `*fuzz*` branches with path filters, `workflow_dispatch`
    with optional `target` input), `permissions: read-all`, concurrency
    group, matrix `{ target: oso, tier: 1, max_total_time: 3600 }`, and a
    `fuzz-corpus-lint` job. Calls `./.github/workflows/build-steps.yml`.
  - `build-steps.yml`: new inputs `fuzz_target`, `fuzz_max_time`,
    `fuzz_corpus_lint`; new steps after Build: restore corpus cache (key
    `fuzz-corpus-<target>-<ref>-<run_id>`, restore-keys as OIIO), Fuzz
    (`src/build-scripts/ci-fuzztest.bash`), save cache (always), upload
    `crash_<target>_*` on failure. (No companion-repo checkout step.)
  - `src/build-scripts/ci-fuzztest.bash`: OIIO's script with
    `OIIO_FUZZ_FORMAT`->`OSL_FUZZ_TARGET`, `--list-formats`->`--list-targets`,
    `$OpenImageIO_ROOT/bin/oiio_fuzz_image`->`$OSL_ROOT/bin/osl_fuzz`,
    `populate_corpora.py --target`, and `-max_len=1048576`. Same merge-then-
    timed-run, same limits (`rss_limit_mb=4096`, `malloc_limit_mb=2048`,
    `timeout=60`), same job-summary text.
  - `.github/workflows/actions.lock` regenerated if the new workflow adds
    action uses (OSL has `lockfile.yml` like OIIO).
- **Container/compiler**: `aswf/ci-osl:2026-clang20` with `cc_compiler: clang`,
  `cxx_compiler: clang++`, `build_type: Release`, `skip_tests: '1'`,
  `setenvs: export SANITIZE=address,undefined OSL_BUILD_FUZZ_TARGETS=ON USE_PYTHON=0 OSL_BUILD_TESTS=0`.
  - **Risk to verify on first CI run**: that this container's clang ships the
    libFuzzer runtime (`libclang_rt.fuzzer*.a`). OIIO's `aswf/ci-oiio:2027`
    does. If not, fall back to installing it (`llvm_action_ver` path already
    in build-steps) or the newest `ci-osl` image that has it.
  - `OSL_BUILD_TESTS=0` skips testshade/testrender (not needed; faster).
    `oslc` is always built, needed for seeding.
- Leak suppressions: OSL's `ci-startup.bash` already exports
  `LSAN_OPTIONS=suppressions=.../nosanitize.txt`; reuse. New benign leaks
  found in the first runs get entries there, as OIIO did.

## R9. Documentation

- **Decision**: `docs/dev/fuzzing.md` with OIIO's section structure:
  prerequisites (upstream clang, not AppleClang), building, listing targets,
  running locally, reproducing a CI crash, minimizing, turning a finding into
  a regression test (`testsuite/oso-<name>/` with the minimized `.oso`, like
  `oso-excess-defaults`), adding a target (future), target selection,
  memory limits / ustring note, CI overview, OSS-Fuzz (deferred).
- Update `AGENTS.md` repo map with one line for `src/fuzz/`.
