# Data Model: OSL Input Fuzzing Infrastructure

No persistent data store. Entities are code structures, files, and CI state.

## FuzzTarget (harness, compile time)

One row in the static target table in `src/fuzz/osl_fuzz.cpp`.

| Field | Type | Notes |
|-------|------|-------|
| `name` | string | Unique, lowercase, `[a-z]+`. Used in env var, `fuzz_<name>` symlink, `--target=`, corpus dir, CI matrix, artifact names. This feature: `oso`. Reserved: `osl`, `scene`. |
| `init` | `void()` | Called once from `LLVMFuzzerInitialize` after target selection; sets up target-specific state. |
| `run` | `void(cspan<uint8_t>)` | Called per input; must return normally for any input (FR-007). |

Validation: table names unique; `--list-targets` prints exactly the table
names, sorted, one per line.

## Harness Process State (runtime, per process)

| State | Lifetime | Notes |
|-------|----------|-------|
| selected target | process | Resolved once (env > argv[0] > `--target=`); immutable afterwards. |
| `RendererServices`, quiet `ErrorHandler`, `ShadingSystem` | process | Created once in the `oso` target's `init`. `allow_shader_replacement=1`. |
| shader master `"fuzz"` | one iteration | Replaced on next load (R3). |
| `ShaderGroupRef` | one iteration | Released at end of `run`. |
| interned ustrings | process | Grow monotonically; bounded in practice by `max_len` and run length (R3). |

State transitions per `oso` input:
`load -> (fail: return) -> group begin/shader/end -> (fail: return) -> optimize (no JIT) -> release group -> return`.

## SeedSource (populate_corpora.py)

`TARGET_SOURCES: dict[target_name, list[SourceSpec]]`

| SourceSpec field | Notes |
|------------------|-------|
| `kind` | `copy` (copy matching files) or `compile_osl` (run oslc, emit `.oso`) |
| `root` | repo-relative directory, searched recursively |
| `exts` | extensions matched |

`oso` sources: `copy testsuite/** *.oso`; `compile_osl testsuite/** *.osl`;
`compile_osl src/shaders/** *.osl`. Per-file cap 1 MiB; no count cap.
Output filenames unique by encoding the relative path.

## Corpora

| Corpus | Location | Persistence |
|--------|----------|-------------|
| Committed corpus dir | `src/fuzz/corpora/<target>/` | Git; `.gitkeep` only for `oso` |
| Gathered seeds | `seeds/<target>/` (CI workspace) | Per run, discarded |
| Evolved corpus | `corpus/<target>/` (CI workspace) | GHA cache, key `fuzz-corpus-<target>-<ref>-<run_id>`, restore-keys by ref then target |
| Reproducers | `crash_<target>_*`, `timeout-*`, `oom-*`, `leak-*` with prefix | Uploaded artifact `fuzz-crashes-<target>-<run_id>`, 30-day retention |

## FuzzJob (CI)

| Field | Value for `oso` |
|-------|-----------------|
| target | `oso` |
| tier | 1 |
| max_total_time | 3600 s |
| status | PASS / FAILED (crash, timeout, OOM, leak, or merge-time finding) / SKIPPED (target not compiled in) |
