---

description: "Task list for OSL input fuzzing infrastructure"
---

# Tasks: OSL Input Fuzzing Infrastructure

**Input**: Design documents from `docs/dev/specs/004-osl-fuzzing/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: No unit-test tasks (not requested). Validation is by the
quickstart scenarios, run as explicit tasks at each checkpoint.

**Organization**: Grouped by user story. In scope: US1 (P1, `.oso` fuzzing
locally) and US2 (P1, nightly CI). US3 (`.osl`), US4 (`scene`), US5 (OSS-Fuzz)
are deferred per spec; no tasks here, but the Polish phase verifies the design
leaves room for them (SC-010).

**Reference**: OIIO's implementation at `~/code/oiio/oiio.lg` (`src/fuzz/`,
`src/build-scripts/ci-fuzztest.bash`, `.github/workflows/fuzz.yml`,
`.github/workflows/build-steps.yml`, `docs/dev/fuzzing.md`). Mirror it unless
research.md says otherwise.

**Conventions for every new file**: OSL copyright + `SPDX-License-Identifier:
BSD-3-Clause` + project URL header (copy from any `src/liboslexec/*.cpp` /
`src/build-scripts/*.bash`); ASCII only; `OSL::print`, never printf/streams;
run `make clang-format` (or `src/build-scripts/run-clang-format.bash`) on C++.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Build option and empty fuzz directory, inert by default.

- [X] T001 Add `set_option (${PROJ_NAME}_BUILD_FUZZ_TARGETS "Build libFuzzer fuzz targets (requires clang)" OFF)` immediately after `set_cache (SANITIZE ...)` in `src/cmake/compiler.cmake`, and inside the existing `if (SANITIZE AND ...)` block add the OIIO #5586 snippet: when `${PROJ_NAME}_BUILD_FUZZ_TARGETS AND CMAKE_COMPILER_IS_CLANG`, `add_compile_options (-fsanitize=fuzzer-no-link)` with OIIO's comment (research.md R6)
- [X] T002 Add `if (OSL_BUILD_FUZZ_TARGETS) add_subdirectory (src/fuzz) endif ()` to root `CMakeLists.txt` after the `src/oslinfo` subdirectory (outside the `OSL_BUILD_TESTS` block)
- [X] T003 [P] Create `src/fuzz/corpora/oso/.gitkeep` (empty)
- [X] T004 [P] Create `src/fuzz/osl_fuzz.options` with `[libfuzzer]`, `max_len = 1048576`, `rss_limit_mb = 4096` (contracts/harness-contract.md "Installed files")

**Checkpoint**: Configure without the option succeeds and is unchanged (quickstart.md step 1). Configure with the option fails only because `src/fuzz/CMakeLists.txt` does not exist yet -- that is expected until T005.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Harness build rules and dispatch core, shared by every target (now and future).

- [X] T005 Create `src/fuzz/CMakeLists.txt` by porting OIIO's `src/fuzz/CMakeLists.txt`: same clang / AppleClang warn-and-`return()` gating (message text naming `OSL_BUILD_FUZZ_TARGETS`), same `_FORTIFY_SOURCE` directory-definition strip, same `LIB_FUZZING_ENGINE` resolution into `OSL_FUZZING_ENGINE`; `add_executable (osl_fuzz osl_fuzz.cpp fuzz_oso.cpp)`, `target_link_libraries (osl_fuzz PRIVATE oslexec)`, compile options `-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer`, link options `${OSL_FUZZING_ENGINE} -fsanitize=address,undefined`, `install_targets (osl_fuzz)`, `install (FILES osl_fuzz.options DESTINATION ${CMAKE_INSTALL_BINDIR})`. Check how other OSL executables (e.g. `src/oslinfo/CMakeLists.txt`) get include dirs/link deps and match that (research.md R6)
- [X] T006 Create `src/fuzz/fuzz_targets.h` (`#pragma once`) declaring `struct FuzzTarget { const char* name; void (*init)(); void (*run)(OSL::cspan<uint8_t>); };` per data-model.md FuzzTarget (FuzzTarget.name, FuzzTarget.init, FuzzTarget.run) and `void oso_init(); void oso_run(OSL::cspan<uint8_t>);`, plus a comment explaining how a future `osl`/`scene` target is added (new `fuzz_<name>.cpp`, declarations here, row in the table in `osl_fuzz.cpp`)
- [X] T007 Create `src/fuzz/osl_fuzz.cpp` with the static target table (one row: `oso`; names must satisfy data-model.md FuzzTarget.name), `LLVMFuzzerInitialize` and `LLVMFuzzerTestOneInput` implementing contracts/harness-contract.md exactly: selection order `OSL_FUZZ_TARGET` env > `argv[0]` basename `fuzz_<name>` > `--target=<name>` (stripped from argv); `--list-targets` prints sorted names, `fflush(stdout)`, `exit(0)`; missing/unknown target prints `osl_fuzz: ...` and available targets to stderr, `exit(1)`; then `OIIO::attribute("threads", 1)` and calls the selected target's `init`. `LLVMFuzzerTestOneInput` calls `run` and returns 0. Port structure and comments from OIIO `fuzz_image.cpp`
- [X] T008 Create a stub `src/fuzz/fuzz_oso.cpp` with empty `oso_init()` / `oso_run()` so the harness links; build with upstream clang per quickstart.md step 3 and check quickstart.md step 4 (dispatch contract) passes

**Checkpoint**: `osl_fuzz --list-targets` prints `oso`; no/unknown target exits 1 with the list; AppleClang/gcc configure warns and skips (quickstart.md step 2).

---

## Phase 3: User Story 1 - Fuzz .oso Loading Locally (Priority: P1) MVP

**Goal**: The `oso` target loads, groups, and optimizes arbitrary bytes; seeds can be gathered; developers can run, reproduce, and minimize locally.

**Independent Test**: quickstart.md steps 5, 6, 7.

- [X] T009 [US1] In `src/fuzz/fuzz_oso.cpp`, implement `oso_init()`: static `OSL::RendererServices`, static quiet `OSL::ErrorHandler` subclass whose `operator()(int, const std::string&)` discards messages unless env `OSL_FUZZ_VERBOSE` is set and not `"0"` (then forwards to the base class), static `OSL::ShadingSystem(&rs, nullptr, &errhandler)`; set attributes `allow_shader_replacement=1` and `lockgeom=1` (research.md R3, R5; data-model.md "Harness Process State")
- [X] T010 [US1] In `src/fuzz/fuzz_oso.cpp`, implement `oso_run()` per contracts/harness-contract.md "oso target behavior": `LoadMemoryCompiledShader("fuzz", string_view of bytes)` -> on failure return; `ShaderGroupBegin("fuzz")`, `Shader(*group, "surface", "fuzz", "layer")`, `ShaderGroupEnd(*group)` -> on any failure return; `optimize_group(group.get(), nullptr, /*do_jit=*/false)`; let the `ShaderGroupRef` go out of scope. Comment why there is no JIT and why the fixed name (R2, R3)
- [X] T011 [US1] Create `src/fuzz/populate_corpora.py` by porting OIIO's script: same argparse shape with `--target NAME` (replacing `--format`), `--dest DIR`, `--dry-run`, plus `--oslc PATH` (default search: `$OSL_ROOT/bin/oslc`, `dist/bin/oslc`, `build/bin/oslc`); `MAX_BYTES = 1 MiB`; `TARGET_SOURCES` per data-model.md SeedSource (SeedSource.kind, SeedSource.root, SeedSource.exts) with `oso` -> `copy testsuite *.oso`, `compile_osl testsuite *.osl`, `compile_osl src/shaders *.osl`; recursive search; compile with `oslc -q -I<shader dir> -o <dest>/<target>/<relpath-with-_>.oso <src>` in a thread pool; count and report copied / compiled / failed-to-compile; skip outputs over `MAX_BYTES`; idempotent; summary format like OIIO's. Comment where future `osl` / `scene` entries go (research.md R7)
- [X] T012 [US1] Run quickstart.md step 5 (seed gather + `-runs=0`). Confirm compiled count meets SC-007 (>= 90% of compilable shaders). Any crash during `-runs=0` is a real finding: record it (input path + sanitizer summary) in a "Findings" list at the end of this tasks.md and stop for user triage rather than working around it in the harness
- [X] T013 [US1] Run quickstart.md step 6 (60 s timed run). Record exec/s and final `cov:` in this tasks.md "Findings" section along with any crashes; watch RSS for ustring growth (R3)
- [X] T014 [US1] Run quickstart.md step 7 (revert #2220 in a scratch worktree/build, replay `testsuite/oso-excess-defaults/param_float_many.oso`, expect ASan heap-buffer-overflow; with fix, clean exit; `OSL_FUZZ_VERBOSE=1` shows the load error). Do not commit the revert
- [X] T015 [P] [US1] Write `docs/dev/fuzzing.md` by porting OIIO's `docs/dev/fuzzing.md` section by section (research.md R9): prerequisites, building, listing targets, running locally, reproducing a CI crash, minimizing, turning a finding into `testsuite/oso-<name>/` regression test (point at `testsuite/oso-excess-defaults/` as the model), seeds and `populate_corpora.py`, target selection, adding a new target (future `osl`/`scene`), memory limits incl. ustring-growth note, CI overview, OSS-Fuzz (deferred). License header as in other `docs/dev/*.md` files
- [X] T016 [P] [US1] Add a one-line `src/fuzz/` entry to the repo map in `AGENTS.md` (under "Command line tools" or a new "Testing/fuzzing" bullet) pointing to `docs/dev/fuzzing.md`

**Checkpoint**: US1 complete and demonstrable locally. Natural point for PR 1 (plan.md "Staging").

---

## Phase 4: User Story 2 - Automated Nightly Fuzz CI (Priority: P1)

**Goal**: Nightly / on-push / on-dispatch fuzzing with cached corpus, reproducer artifacts, and corpus lint.

**Independent Test**: quickstart.md step 8.

- [X] T017 [US2] Create `src/build-scripts/ci-fuzztest.bash` by porting OIIO's script per contracts/ci-contract.md: env `OSL_FUZZ_CORPUS_LINT`, `OSL_FUZZ_TARGET`, `OSL_FUZZ_MAX_TIME`; binary `$OSL_ROOT/bin/osl_fuzz`; `--list-targets`; seeding via `populate_corpora.py --target "$OSL_FUZZ_TARGET" --dest seeds --oslc "$OSL_ROOT/bin/oslc"`; add `-max_len=1048576` to merge and timed run; otherwise identical flags, merge-then-run logic, status handling, and `$GITHUB_STEP_SUMMARY` text (with "Target:" instead of "Format:"). Use `$TMPDIR`-safe temp files for the lint lists. `chmod +x`. FuzzJob.status values per data-model.md FuzzJob.status
- [X] T018 [US2] Add to `.github/workflows/build-steps.yml`: inputs `fuzz_target` (string, ''), `fuzz_max_time` (string, '3600'), `fuzz_corpus_lint` (boolean, false) next to the other inputs; and, after the `Build` step (before `ccache-save` is fine, or after -- match OIIO's placement relative to Build), steps "Restore fuzz corpus cache", "Fuzz" (id `fuzz`, env mapping to `OSL_FUZZ_*`, `run: src/build-scripts/ci-fuzztest.bash`), "Save fuzz corpus cache" (`if: always() && inputs.fuzz_target != ''`), "Upload fuzz crash artifacts" (`fuzz-crashes-<target>-<run_id>`, `crash_<target>_*`, `if-no-files-found: ignore`, 30-day retention). Cache key scheme per data-model.md "Corpora" (Evolved corpus row). Use the same action versions already used elsewhere in this file / OSL's `actions.lock`; omit OIIO's oiio-images checkout step
- [X] T019 [US2] Create `.github/workflows/fuzz.yml` by porting OIIO's: header comment about `actions.lock`, name `Fuzz`, triggers (cron `0 10 * * *` on `AcademySoftwareFoundation/OpenShadingLanguage` only; push/PR on `*fuzz*` branches with OIIO's path filters; `workflow_dispatch` with optional `target` input), `permissions: read-all`, concurrency group; job `fuzz` named `"Fuzz ${{ matrix.target }} (tier ${{ matrix.tier }})"`, `fail-fast: false`, matrix single entry `{ target: oso, tier: 1, max_total_time: 3600 }` (data-model.md FuzzJob), filtered by the dispatch input when non-empty; `uses: ./.github/workflows/build-steps.yml` with `runner: ubuntu-latest`, `nametag: fuzz-linux-clang`, `container: aswf/ci-osl:2026-clang20`, `cc_compiler: clang`, `cxx_compiler: clang++`, `build_type: Release`, `cxx_std: 20`, `python_ver`/`simd` matching the vfx2026 job in `ci.yml`, `skip_tests: '1'`, `fuzz_target`, `fuzz_max_time`, `setenvs: export SANITIZE=address,undefined OSL_BUILD_FUZZ_TARGETS=ON USE_PYTHON=0 OSL_BUILD_TESTS=0`; job `fuzz-corpus-lint` as OIIO with `fuzz_corpus_lint: true` and `OSL_BUILD_FUZZ_TARGETS=ON USE_PYTHON=0 OSL_BUILD_TESTS=0`. Check every input name used actually exists in OSL's `build-steps.yml` (OSL's set differs from OIIO's)
- [X] T020 [US2] Regenerate `.github/workflows/actions.lock` if `fuzz.yml`/`build-steps.yml` introduced any new `uses:` (follow the instructions in the lockfile header / `lockfile.yml`); if `gh actions-lock` is unavailable locally, note it for the user rather than hand-editing
- [ ] T021 [US2] Verify CI: ask the user to push the branch (only the user pushes/opens PRs). Then check the run with `gh run list` / `gh run view`: lint passes; the oso job builds (confirm the container clang has the libFuzzer runtime -- research.md R8 risk; if not, propose a fix to the user), seeds, merges, runs, and saves cache. Record outcome and any findings in this tasks.md "Findings" section

**Checkpoint**: US2 complete. Natural point for PR 2 (or fold into PR 1).

---

## Phase 5: Polish & Cross-Cutting Concerns

- [X] T022 [P] Verify normal build unaffected (SC-008): fresh default configure shows `OSL_BUILD_FUZZ_TARGETS=OFF`, no `src/fuzz` targets; existing ctest suite results unchanged versus `main` on a quick subset (e.g. `ctest -R oso-`)
- [X] T023 [P] Extensibility check (SC-010): walk through, on paper, adding the deferred `osl` target and confirm it touches only `src/fuzz/fuzz_osl.cpp`, `fuzz_targets.h`, the table in `osl_fuzz.cpp`, `src/fuzz/CMakeLists.txt` source list + `oslcomp` link, `corpora/osl/`, a `TARGET_SOURCES` entry, one matrix line, and a docs subsection. Fix any place where that is not true
- [X] T024 [P] OIIO-parity check (SC-009): diff each ported file against its OIIO counterpart and confirm every divergence is either a name substitution or justified in research.md; add a note to research.md for any unlisted divergence
- [X] T025 Run `make clang-format` (or `src/build-scripts/run-clang-format.bash`) and confirm `src/fuzz/*.cpp`/`*.h` are clean; confirm all new files have the license header and are ASCII-only (`LC_ALL=C grep -nP '[^\x00-\x7F]'`)
- [X] T026 Update `spec.md` Status line to reflect what was implemented and mark US3-US5 as still deferred; update `checklists/requirements.md` if anything changed

---

## Dependencies & Execution Order

- Phase 1 -> Phase 2 -> US1 -> US2 -> Polish.
- T001, T002 sequential (both edit cmake config); T003, T004 parallel with them.
- T005 -> T006 -> T007 -> T008 (each builds on the previous).
- US1: T009 -> T010 (same file); T011 can start any time after Phase 1 (independent file); T012 needs T010 + T011; T013 needs T012; T014 needs T010; T015, T016 parallel with everything in US1.
- US2 depends on US1 (CI runs the harness and seed script). T017 -> T018 -> T019 -> T020 -> T021.
- Polish after US2 (T022-T024 parallel).

## Parallel Example: User Story 1

```text
In parallel after T008:
  T009/T010  fuzz_oso.cpp (harness target)
  T011       populate_corpora.py (seed script)
  T015       docs/dev/fuzzing.md
  T016       AGENTS.md
```

## Implementation Strategy

- **MVP**: Phases 1-3 (US1). Fully usable by a developer locally; maps to PR 1.
- **Increment 2**: Phase 4 (US2) CI; PR 2 (or same PR, as OIIO #5314 did).
- **Findings**: any defects surfaced in T012/T013/T021 are fixed in separate PRs, each with a `testsuite/oso-<name>/` regression test; they are not part of this task list.
- Keep each task a small, reviewable change; stop at each checkpoint for user review.

## Findings

Recorded 2026-11-19, local macOS arm64 run (brew llvm@22 clang, Release,
ASan+UBSan, fuzzer-no-link libs). Not fixed here; each needs its own PR with
a regression test.

1. **CRASH -- heap-buffer-overflow WRITE in `OSOProcessorBase::find_conditionals()`**
   (`src/liboslexec/runtimeoptimize.cpp:1097`), found in the first seconds of
   the T013 timed run from a single `ChangeByte` mutation of a compiled
   testsuite shader. A jump target (op argument of `if`/`for`/etc.) beyond the
   op count is accepted by the `.oso` loader, and `std::fill(...begin()+i,
   begin()+farthest_jump())` writes past `m_in_conditional`. Likely fix: the
   loader (or `ShaderMaster::resolve_syms`) validates every jump address is in
   `[0, ops.size()]` and rejects the shader. Reproducer (11 KB):
   `build/fuzz/findings/crash-292f6e9c81ec2d6c5c44bf65902eea741427d3d5`
   (gitignored build dir).
2. **UBSan (recoverable) -- signed overflow `2147483647 + 1`** in
   `RuntimeOptimizer::insert_code` (`runtimeoptimize.cpp:593`) via
   `constfold_getattribute` (`constfold.cpp:2367`): a symbol's lastread of
   INT_MAX ("never") is incremented when ops are inserted. Triggered by a
   valid testsuite seed during T012.
3. **UBSan (recoverable) -- left shift of 257 by 31 in OIIO `fast_safe_pow`**
   (`OpenImageIO/fmath.h:2117`) via `constfold_pow` (`constfold.cpp:1853`).
   Valid testsuite seed. Bug is in OIIO's header; OSL may also want to avoid
   the call for such inputs.
4. **UBSan (recoverable) -- misaligned construction at startup**:
   `src/include/OSL/sfm_simplex.h:185` declares
   `alignas(64) static const OSL::Block<Vec4,32> lut_wide` but the type
   requires 128-byte alignment. Fires on every process start (static init in
   `simplexnoise.cpp`), so it shows up as noise in every fuzz/sanitizer run.

5. **UBSan (recoverable) -- bad downcast in the compiler** (found by CI run
   44471073927, Linux): `ASTNode::one_default_literal`
   (`src/liboslcomp/codegen.cpp:623`) does `static_cast<ASTliteral*>(init)`
   before checking `islit`. All uses are guarded by `islit`, so it is benign
   in practice, but UBSan's vptr check reports it, and the demangling it does
   for the report leaks 128 bytes, so LeakSanitizer fails `oslc` on
   `src/shaders/image.osl` and `mandelbrot.osl`. That broke the fuzz job's
   build. FIXED on this branch (and separately on branch `lg-codegen-ub` for
   its own PR): only downcast when `islit`. Independently, the fuzz workflow
   now passes `-DOSL_BUILD_SHADERS=OFF`: the fuzz build has no use for the
   installed shaders (the seed script compiles them itself, with leak
   detection off), so this also saves build time and keeps a future
   compiler UB report from breaking the fuzz build before fuzzing starts.

Performance (T012/T013): 1425 seeds (1416 compiled of 1438 shaders = 98.5%,
22 intentional compile failures, 9 hand-written .oso). Seeds-only pass: no
crash, cov 11809, ft 46826, ~490 exec/s, RSS 725 MB.

T014: with #2220's loader fix reverted, replaying
`testsuite/oso-excess-defaults/param_float_many.oso` is caught (deadly
signal in `OSOReaderToMaster::add_param_default`; the libc++ hardening trap
fires before ASan); with the fix, clean rejection, and `OSL_FUZZ_VERBOSE=1`
shows "too many initial values".

T021: first CI run 44471073927 -- corpus lint passed (the
`aswf/ci-osl:2026-clang20` clang has the libFuzzer runtime, resolving the
research.md R8 risk); the oso job failed in Build (finding 5). Re-run pending.
