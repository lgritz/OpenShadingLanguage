# Feature Specification: OSL Input Fuzzing Infrastructure

**Feature Branch**: `lg-fuzz`

**Created**: 2026-11-19

**Status**: Implemented (User Stories 1-2; CI pending first run). User
Stories 3-5 (`osl`, `scene`, OSS-Fuzz) remain deferred.

**Input**: User description: "libFuzzer fuzzing for OSL, mirroring OIIO's approach"

**Model**: This feature deliberately mirrors OpenImageIO's fuzzing infrastructure
(OIIO spec `docs/dev/specs/001-image-fuzzing/`, PR #5314 and follow-ups through
#5617). The two projects are maintained by the same team, so build options, CI
structure, harness conventions, corpus handling, and documentation SHOULD be
analogous unless an OSL-specific reason requires a difference. Where this spec
diverges from OIIO, it says so and why.

## User Scenarios & Testing *(mandatory)*

The inputs OSL accepts from untrusted or semi-trusted sources are, in order of
importance:

1. **Compiled shader bytecode (`.oso`)** -- loaded by every renderer that embeds
   OSL, frequently shipped between studios and vendors, and already the subject
   of a real memory-safety report (fixed in #2220).
2. **Shader source (`.osl`)** -- compiled by `oslc` and by renderers that compile
   shaders at runtime from source buffers.
3. **testrender scene files** -- XML scene descriptions read by the reference
   renderer used throughout the testsuite.

In OIIO the "unit" that a single harness binary dispatches over is an image
*format*. In OSL the analogous unit is an *input type* ("target"): `oso`,
`osl`, `scene`.

**Scope of this feature**: only the `oso` target is implemented now (Stories
1-2). The `osl` and `scene` targets (Stories 3-4) are **deferred** and recorded
here so the design leaves a clear place for them: adding either later MUST be an
additive change (new target in the harness, corpus directory, seed sources,
matrix entry) with no rework of the infrastructure built here.

### User Story 1 - Fuzz .oso Loading Locally (Priority: P1)

A developer wants to throw malformed and adversarial `.oso` files at OSL's
bytecode loader and the stages that consume a loaded shader, under memory and
undefined-behavior sanitizers, to find crashes before users do. They configure a
fuzz-enabled build, run one binary pointed at the `oso` target with a seed
corpus, and get a reproducible crash file plus sanitizer report for any defect.

**Why this priority**: `.oso` is the highest-risk OSL input and the first one
with a confirmed memory-safety bug. This story also establishes the build
option, harness binary, dispatch mechanism, and documentation that every later
story reuses -- it is the MVP.

**Independent Test**: Configure with the fuzz build option and a suitable
clang, build the harness, run it on the `oso` target for 60 seconds against the
generated seed corpus, and confirm it runs without crashing. Feed it the
pre-#2220 reproducer (an `.oso` with excess initializer values) against a
build with that fix reverted, and confirm it reports a heap overflow.

**Acceptance Scenarios**:

1. **Given** a fuzz-enabled build, **When** the developer runs the harness with
   the `oso` target selected and a seed directory, **Then** it processes every
   seed and begins mutation without additional setup.
2. **Given** a valid `.oso` input, **When** the harness processes it, **Then**
   the shader is loaded and carried through the post-load stages in scope
   (see FR-004) without crashing.
3. **Given** a zero-byte, truncated, or random-byte input, **When** the harness
   processes it, **Then** OSL rejects it through its normal error path and the
   harness returns gracefully.
4. **Given** a crash reproducer file, **When** passed as the sole argument to
   the harness, **Then** the crash reproduces deterministically with a full
   sanitizer stack trace.
5. **Given** the harness is run with no target selected, **When** it starts,
   **Then** it prints the list of available targets and exits non-zero.

---

### User Story 2 - Automated Nightly Fuzz CI (Priority: P1)

A maintainer wants every fuzz target exercised automatically every night (and
on demand), with any crash, sanitizer violation, timeout, out-of-memory, or leak
failing the run and attaching a reproducer, exactly as OIIO's nightly fuzz
workflow does.

**Why this priority**: Without CI, fuzzing only happens when someone remembers
to run it. This delivers continuous value with no developer action.

**Independent Test**: Push to a branch whose name contains `fuzz`. Confirm the
workflow builds the harness, seeds and minimizes the corpus, runs each target
in its own parallel job for its budget, and passes. Introduce a deliberate bug
reachable from a seed and confirm the corresponding job fails and uploads a
reproducer artifact naming the target.

**Acceptance Scenarios**:

1. **Given** the nightly schedule fires on the canonical repository, **When**
   all target jobs finish, **Then** the workflow reports success and uploads no
   crash artifacts.
2. **Given** a target finds a defect, **When** its job completes, **Then** that
   job fails, uploads its reproducer(s) as an artifact, and writes the target
   name and failure summary to the job summary; other targets' jobs still run
   to completion.
3. **Given** a manual dispatch naming one target, **When** the workflow runs,
   **Then** only that target is fuzzed.
4. **Given** a target is added to the harness without a corresponding corpus
   directory, **When** the corpus-lint job runs, **Then** it fails with a
   message naming the missing directory.
5. **Given** a previous run's evolved corpus is cached, **When** the next run
   starts, **Then** it resumes from that corpus rather than from seeds alone.

---

### User Story 3 - Fuzz .osl Compilation (Priority: P2, deferred)

*Deferred: not implemented in this feature. Kept to constrain the design so it
can be added later as a new target without restructuring.*

A developer wants malformed shader source fed through OSL's compiler front end
(preprocessing, lexing, parsing, type checking, code generation, and bytecode
emission) to find crashes, hangs, and memory errors in the compiler.

**Why this priority**: Shader source is the second most important input. The
infrastructure from Stories 1-2 makes this an additional target rather than new
plumbing.

**Independent Test**: Run the harness on the `osl` target for 60 seconds
against seeds drawn from the testsuite's shader sources; confirm it runs without
crashing and that its CI job runs nightly alongside `oso`.

**Acceptance Scenarios**:

1. **Given** a valid shader source input, **When** the harness processes it,
   **Then** it compiles successfully (or fails with an ordinary compile error)
   without crashing.
2. **Given** syntactically or semantically invalid source, **When** processed,
   **Then** the compiler reports errors through its normal path and the harness
   returns gracefully.
3. **Given** source containing preprocessor include directives, **When**
   processed, **Then** the standard OSL header resolves and no file outside the
   intended include locations is required for the harness to behave
   deterministically.

---

### User Story 4 - Fuzz testrender Scene Files (Priority: P3, deferred)

*Deferred: not implemented in this feature. Kept to constrain the design so it
can be added later as a new target without restructuring.*

A developer wants malformed testrender scene descriptions fed through the scene
reader to find crashes in scene parsing and scene setup.

**Why this priority**: Least important of the three inputs (testrender is a
reference/test renderer, not a production component), and the only one that
requires restructuring existing code before a harness can link against it.

**Independent Test**: Run the harness on the `scene` target for 60 seconds
against seeds from the testsuite's scene files; confirm it runs without
crashing, and that testrender's existing tests still pass unchanged.

**Acceptance Scenarios**:

1. **Given** a valid scene input, **When** processed, **Then** it is parsed and
   set up to the depth defined in FR-012 without crashing.
2. **Given** malformed XML or a scene with invalid/missing elements or
   attributes, **When** processed, **Then** the reader reports an error and
   returns, rather than terminating the process.
3. **Given** a scene referencing external files (meshes, shaders by name),
   **When** processed, **Then** the harness behaves deterministically whether
   or not those files exist.
4. **Given** testrender's scene reader has been restructured for fuzzing,
   **When** the full testsuite runs, **Then** all testrender tests produce the
   same results as before.

---

### User Story 5 - OSS-Fuzz Onboarding Readiness (Priority: P3, deferred)

As in OIIO, the harness and build structure should make a later OSS-Fuzz
submission a small, additive step. Actual onboarding is out of scope for this
feature.

**Why this priority**: Stated future goal shared with OIIO; costs almost
nothing if the structure is right from the start.

**Independent Test**: Inspection: the harness uses the standard fuzzing entry
points, links against the externally supplied fuzzing engine when one is
provided, and supports per-target dispatch by executable name.

**Acceptance Scenarios**:

1. **Given** an externally supplied fuzzing engine, **When** the harness is
   built, **Then** it links against that engine with no source changes.
2. **Given** the harness is invoked through a `fuzz_<target>` named link,
   **When** it starts, **Then** it selects that target.

---

### Edge Cases

- **Unbounded interned-string growth**: OSL interns strings permanently. Every
  novel identifier or string literal the fuzzer produces is retained for the
  life of the process, so memory grows slowly across iterations. The harness
  and CI configuration must keep this from tripping the fuzzer's memory limit
  as a false positive (or document it as a known constraint).
- **Accumulating loaded shaders**: Loaded shader definitions are retained by
  name. Repeated loads must not accumulate unboundedly across iterations.
- **Internal assertion failures**: An OSL internal assertion that fires on bad
  input aborts the process. This is treated as a finding (the input should have
  been rejected earlier), not a harness defect.
- **Huge declared sizes**: `.oso` or `.osl` declaring enormous array lengths or
  symbol counts must be rejected or bounded rather than allocating into an
  out-of-memory kill; harness memory budgets must be commensurate with OSL's
  own limits (as OIIO lowered its image-size limits to fit the fuzzer budget).
- **Hangs**: Inputs that make the optimizer, compiler, or preprocessor loop
  without terminating must be detected via a per-input timeout and reported.
- **Error output volume**: Malformed inputs produce many error messages; the
  harness must not let error printing dominate throughput or fill CI logs.
- **Filesystem reach** *(deferred targets)*: Preprocessor includes and scene references can name
  arbitrary paths; the harness must not depend on, or modify, files outside its
  control.
- **Process-terminating error handling** *(deferred `scene` target)*: Any code path that exits the process
  on bad input (currently testrender's scene reader) is a harness-visible
  failure and must be converted to an error return in scope for that target.
- **Missing companion assets**: Unlike OIIO, OSL has no separate companion
  repository of sample inputs; seeds must come from the main repository.

## Clarifications

### Session 2026-11-19

- Q: How deep past loading should the `oso` target go? -> A: Load, build a
  shader group, and run runtime optimization, without native (LLVM) code
  generation.
- Q: Which targets are in scope now? -> A: Only `oso`. `osl` and `scene` are
  deferred; the design must leave a place to add them later, but they are not
  implemented, and no testrender restructuring is done in this feature.

## Requirements *(mandatory)*

Requirements tagged **(deferred)** belong to Stories 3-4. They are not
implemented in this feature; they are recorded so the design accommodates them.

### Functional Requirements

- **FR-001**: The system MUST provide a single fuzz harness binary that
  dispatches over fuzz targets and selects one target per process. In this
  feature the only target is `oso`; the dispatch mechanism MUST allow `osl`
  and `scene` to be added later as additional targets,
  mirroring OIIO's single dynamic-dispatch binary. Target selection priority
  MUST mirror OIIO: environment variable, then executable name (`fuzz_<target>`),
  then a `--target=<name>` argument. A `--list-targets` argument MUST print the
  targets compiled into this build, one per line.
- **FR-002**: The fuzz build MUST be controlled by a build option
  (`OSL_BUILD_FUZZ_TARGETS`, analogous to `OIIO_BUILD_FUZZ_TARGETS`) that
  defaults to OFF and does not affect normal builds. With an unsuitable
  compiler (non-clang, or Apple's clang which lacks the fuzzer runtime), the
  build MUST warn and skip the fuzz targets rather than fail.
- **FR-003**: The harness MUST be built with address and undefined-behavior
  sanitizers; when the project-wide sanitizer option is also enabled, the OSL
  libraries themselves MUST additionally receive fuzzer coverage
  instrumentation (as OIIO does) so the fuzzer can steer into library code.
- **FR-004**: The `oso` target MUST accept arbitrary bytes as a compiled shader,
  load it through the public shading-system API, and, when loading succeeds,
  build a shader group from it and run runtime optimization on that group,
  without native code generation. Each iteration MUST leave no loaded-shader
  or group state that accumulates across iterations.
- **FR-005** *(deferred)*: The `osl` target MUST accept arbitrary bytes as shader source and
  compile it in memory through the public compiler API, with the standard OSL
  header resolvable and no output files written.
- **FR-006** *(deferred)*: The `scene` target MUST accept arbitrary bytes as a testrender
  scene description and process it through the same scene-reading code that
  testrender uses (not a copy).
- **FR-007**: No target may terminate the process on malformed input via a
  normal error path; malformed input MUST produce an error report and a normal
  return. *(Deferred part: when the `scene` target is added, testrender's
  scene reader, which currently exits on errors, MUST be changed to return an
  error, preserving testrender's command-line behavior.)*
- **FR-008**: The harness MUST suppress or cheaply discard OSL error and
  warning output during fuzzing, while still allowing it to be shown when
  reproducing a single input.
- **FR-009**: The harness MUST configure OSL for single-threaded, deterministic
  operation and set any OSL resource limits so that oversized inputs are
  rejected before exceeding the fuzzer's memory budget (analogous to OIIO's
  `OIIO_FUZZ_INIT`).
- **FR-010**: A seed-gathering script (analogous to OIIO's
  `populate_corpora.py`) MUST collect seeds per target from the repository,
  organized so that adding a target means adding its seed sources only. For
  `oso`: compile the testsuite and bundled shader sources with the just-built
  compiler, plus any committed hand-written `.oso` fixtures (e.g. regression
  tests for loader bugs). *(Deferred: `osl` would draw from the same shader
  sources directly; `scene` from testsuite scene files.)* Seeds are capped
  per-file by size, not by count; coverage-based minimization happens
  downstream.
- **FR-011**: Each target MUST have a corpus directory in the repository (a
  placeholder is sufficient); CI MUST fail a lint check if any target reported
  by `--list-targets` lacks one.
- **FR-012** *(deferred)*: The `scene` target MUST parse the scene and
  perform scene setup short of rendering any pixels. Exact depth (e.g.
  whether inline shaders are compiled) is to be decided when the target is
  taken up.
- **FR-013**: A GitHub Actions workflow MUST run every target nightly on the
  canonical repository, on pushes/PRs from branches whose name contains
  `fuzz`, and on manual dispatch with an optional single-target input,
  using a parallel matrix (one job per target, `fail-fast: false`).
- **FR-014**: Per-target time budgets MUST be declared explicitly in the
  workflow matrix, using OIIO's tiers: `oso` in the longer tier (60 minutes,
  matching OIIO Tier 1). *(Deferred: `osl` in the longer tier, `scene` in the
  shorter 30-minute tier.)* Adding a target MUST be a one-line matrix
  addition.
- **FR-015**: Each CI job MUST restore a per-target evolved corpus from cache,
  merge freshly gathered seeds into it keeping only coverage-increasing inputs,
  run the timed fuzz session, and save the corpus back to cache -- the same
  sequence as OIIO's `ci-fuzztest.bash`.
- **FR-016**: On any crash, sanitizer violation, timeout, out-of-memory, or
  leak (including one found during corpus merge), the job MUST fail, upload
  reproducer artifacts named by target, and write a job summary.
- **FR-017**: The harness MUST use the standard `LLVMFuzzerTestOneInput` /
  `LLVMFuzzerInitialize` entry points and link against an externally supplied
  fuzzing engine when one is provided (`LIB_FUZZING_ENGINE`), defaulting to
  libFuzzer locally.
- **FR-018**: The harness MUST run on a developer workstation (Linux or macOS
  with upstream clang) with no tooling beyond the fuzz build configuration.
- **FR-019**: Developer documentation (`docs/dev/fuzzing.md`, analogous to
  OIIO's) MUST cover building, listing targets, running locally, gathering
  seeds, reproducing and minimizing a CI crash, memory-limit rationale, and
  turning a finding into a testsuite regression test.
- **FR-020**: The harness binary MUST be named with an OSL prefix so an
  accidental install is unambiguous (analogous to `oiio_fuzz_image`).

### Key Entities

- **Fuzz Target**: One OSL input type (`oso`, `osl`, `scene`) that the harness
  can be pointed at; analogous to an OIIO image format.
- **Harness**: The single binary that dispatches each input to the selected
  target's processing path.
- **Seed Corpus**: Inputs gathered per target from the repository before a run.
- **Evolved Corpus**: The per-target corpus grown by the fuzzer and persisted
  across CI runs via cache.
- **Reproducer**: An input that triggers a defect; after triage, minimized and
  committed as a testsuite regression test.
- **Fuzz Workflow**: The CI workflow orchestrating per-target jobs, budgets,
  caching, lint, and artifact upload.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The `oso` target builds and runs for at least 60 seconds
  against its gathered seed corpus without a crash on a clean main branch (or
  every crash found is filed and fixed before nightly CI is enabled).
- **SC-002**: Every `oso` seed is processed without crashing in a
  seeds-only (no mutation) pass.
- **SC-003**: Any defect found during a nightly run fails that run and makes a
  reproducer available for download in the same run, 100% of the time.
- **SC-004**: A developer can reproduce a CI-found crash locally from the
  downloaded artifact in under 5 minutes following the documentation.
- **SC-005**: A developer new to the feature can build and run one target
  locally within 15 minutes of reading the documentation.
- **SC-006**: Each nightly target job completes within its declared time budget
  plus build time, well under the CI per-job limit.
- **SC-007**: The `oso` seed corpus contains compiled bytecode for at least 90%
  of the repository's shader sources that compile successfully.
- **SC-008**: Normal (non-fuzz) builds and the full testsuite are unaffected:
  identical configure defaults, and all existing tests pass.
- **SC-009**: Wherever practical, a reviewer familiar with OIIO's fuzzing can
  locate each corresponding OSL piece (build option, harness, seed script,
  CI script, workflow, docs) at the analogous path and name.
- **SC-010**: Adding a deferred target later touches only target-specific
  pieces (harness target function, corpus directory, seed sources, matrix
  line, docs section); no change to build option logic, CI script control
  flow, or workflow structure.

## Assumptions

- This feature delivers infrastructure + `oso` target + CI only. The `osl`
  target and the testrender scene-reader restructuring + `scene` target are
  possible future features that would build on this one.
- Fuzzing targets *reading/compiling* inputs only. Executing shaders on
  arbitrary inputs (shading-time behavior) and the batched/SIMD and GPU
  backends are out of scope.
- Native code generation is not instrumented by the fuzzer (LLVM is a
  prebuilt, uninstrumented dependency), so exercising it gives little
  coverage feedback relative to its cost per input.
- CI runs on Linux using the same ASWF OSL CI container family and clang used
  by existing OSL CI jobs; macOS/Windows CI fuzzing is out of scope.
- The existing testsuite shader sources (well over a thousand) and scene files
  are suitable seed material with no licensing concerns.
- Findings are triaged by maintainers; no automated issue filing.
- Fixes for defects the fuzzer finds are separate PRs, each with its own
  regression test, as was done for OIIO (e.g. #2220 for OSL).
- OSS-Fuzz onboarding is deferred, as in OIIO.
- Structured/API-level fuzzing (e.g. FuzzTest against shading-system APIs) is
  out of scope, as in OIIO.
