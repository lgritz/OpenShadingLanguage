# Implementation Plan: OSL Input Fuzzing Infrastructure

**Branch**: `lg-fuzz` | **Date**: 2026-11-19 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `docs/dev/specs/004-osl-fuzzing/spec.md`

## Summary

Add libFuzzer-based fuzzing to OSL, structured as a near-copy of OIIO's
fuzzing infrastructure. One harness binary, `osl_fuzz`, dispatches over
"targets" (OSL's analog of OIIO's image formats); this feature implements only
the `oso` target, which loads arbitrary bytes as compiled bytecode, builds a
one-layer shader group, and runs the runtime optimizer (no JIT). A seed script
compiles the repo's ~1440 shader sources to `.oso`; a nightly GitHub Actions
workflow seeds, minimizes, fuzzes, caches the evolved corpus, and uploads
reproducers. `osl` and `scene` targets are deferred, but each would be an
additive change (one source file, one table row, one seed entry, one matrix
line). See [research.md](research.md) for decisions.

## Technical Context

**Language/Version**: C++17 (harness), CMake, Bash, Python 3 (seed script),
GitHub Actions YAML

**Primary Dependencies**: clang with libFuzzer + ASan + UBSan (upstream LLVM,
not AppleClang); OSL's `liboslexec`; `oslc` (seeding only)

**Storage**: Files only: per-target corpus dirs; GHA cache for evolved corpus

**Testing**: Manual/CI: `osl_fuzz -runs=0` over seeds (seeds-only pass), timed
local runs, the nightly workflow itself, a known-bad reproducer replayed
against a build with #2220 reverted. Existing ctest suite must be unaffected.

**Target Platform**: Linux CI (ASWF `ci-osl` container, clang); Linux/macOS
dev workstations with upstream clang

**Project Type**: Library test infrastructure (fuzz harness + build/CI glue)

**Performance Goals**: Throughput high enough for useful coverage in a
60-minute job; no specific exec/s target. Seeding (compile ~1440 shaders)
should take a few minutes at most.

**Constraints**: Fuzz option OFF by default; zero effect on normal builds and
ABI. Memory: libFuzzer `rss_limit_mb=4096`, `malloc_limit_mb=2048`,
`max_len=1 MiB`, `timeout=60s`. No production-code changes required for this
feature (fixes for findings are separate PRs).

**Scale/Scope**: ~10 new files, small edits to `CMakeLists.txt`,
`src/cmake/compiler.cmake`, `.github/workflows/build-steps.yml`, `AGENTS.md`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

`.specify/memory/constitution.md` is the unfilled template; no ratified
principles. Gates applied instead from `AGENTS.md` / project conventions:

| Gate | Status |
|------|--------|
| Preserve API/ABI/behavior unless required | PASS -- no library changes |
| New files carry copyright + SPDX header | PASS -- planned for every file |
| ASCII only, clang-format, OSL naming/spacing conventions | PASS -- planned |
| `OSL::print`, not printf/streams | PASS -- harness uses `OSL::print` |
| Error handling via bool/status, not exceptions | PASS |
| Narrow, reviewable PRs | PASS -- see staging below |
| Update `AGENTS.md` when paths/commands change | PASS -- one repo-map line |
| Mirror OIIO (user requirement) | PASS -- see research R1-R9 for each divergence |

Post-design re-check: PASS, no violations; Complexity Tracking not needed.

## Project Structure

### Documentation (this feature)

```text
docs/dev/specs/004-osl-fuzzing/
|-- spec.md
|-- plan.md              # This file
|-- research.md          # Phase 0
|-- data-model.md        # Phase 1
|-- quickstart.md        # Phase 1
|-- contracts/
|   |-- harness-contract.md
|   `-- ci-contract.md
|-- checklists/requirements.md
`-- tasks.md             # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

OIIO counterpart in the right column.

```text
CMakeLists.txt                         # + add_subdirectory(src/fuzz) if OSL_BUILD_FUZZ_TARGETS
src/cmake/compiler.cmake               # + OSL_BUILD_FUZZ_TARGETS option, fuzzer-no-link under SANITIZE
src/fuzz/
|-- CMakeLists.txt                     # = OIIO src/fuzz/CMakeLists.txt
|-- osl_fuzz.cpp                       # ~ OIIO fuzz_image.cpp: init, dispatch, LLVMFuzzer* entry points
|-- fuzz_oso.cpp                       # (new) oso target
|-- fuzz_targets.h                     # (new) target table declaration shared by the above
|-- osl_fuzz.options                   # = oiio_fuzz_image.options (max_len 1 MiB)
|-- populate_corpora.py                # = OIIO populate_corpora.py (targets, oslc compile)
`-- corpora/
    `-- oso/.gitkeep                   # = corpora/<format>/.gitkeep
src/build-scripts/ci-fuzztest.bash     # = OIIO ci-fuzztest.bash
.github/workflows/fuzz.yml             # = OIIO fuzz.yml (matrix: oso)
.github/workflows/build-steps.yml      # + fuzz_target/fuzz_max_time/fuzz_corpus_lint inputs + steps
.github/workflows/actions.lock         # regenerated if needed
docs/dev/fuzzing.md                    # = OIIO docs/dev/fuzzing.md
AGENTS.md                              # + repo-map line for src/fuzz/
```

Future targets (not in this feature) add `src/fuzz/fuzz_osl.cpp` /
`fuzz_scene.cpp`, a row in the target table, `corpora/<target>/`, a
`TARGET_SOURCES` entry, a matrix line, and a docs subsection.

**Structure Decision**: Single top-level `src/fuzz/` directory, same as OIIO,
built only when `OSL_BUILD_FUZZ_TARGETS=ON`.

## Staging (PRs)

Each independently reviewable (per user preference for small chunks):

1. **Build + harness + oso target + docs**: CMake option, `src/fuzz/`
   (harness, options, `corpora/oso/.gitkeep`), `populate_corpora.py`,
   `docs/dev/fuzzing.md`, `AGENTS.md`. Locally verifiable via quickstart.
2. **CI**: `ci-fuzztest.bash`, `build-steps.yml` inputs/steps, `fuzz.yml`,
   lockfile. Verified by pushing to a `*fuzz*` branch.
3. **Fixes for findings**: one PR per defect with a `testsuite/oso-<name>/`
   regression test (outside this feature's scope, but expected).

Stages 1 and 2 could reasonably be a single PR if small enough; OIIO's
#5314 combined them.

## Risks

- `ci-osl` container clang may lack the libFuzzer runtime (research R8);
  verify on first CI run.
- Slow RSS growth from interned strings (R3); monitored, documented.
- Runtime optimizer may assert/crash on valid-but-unusual seeds, making the
  first merge step fail; those are real findings, triaged before enabling the
  nightly schedule on the canonical repo.

## Complexity Tracking

Not applicable; no gate violations.
