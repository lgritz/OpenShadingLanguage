# Contract: `osl_fuzz` harness

Mirrors OIIO's `oiio_fuzz_image` contract
(`OpenImageIO/docs/dev/specs/001-image-fuzzing/contracts/harness-contract.md`)
with "format" -> "target".

## Entry points

- `extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)` -- one-time
  setup, target selection, pseudo-arg handling. Returns 0.
- `extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)` --
  dispatches to the selected target's `run`. Always returns 0.

Links `$LIB_FUZZING_ENGINE` if set in the environment at configure time,
else `-fsanitize=fuzzer`.

## Target selection (first match wins)

1. Env `OSL_FUZZ_TARGET=<name>`.
2. `basename(argv[0])` == `fuzz_<name>` (OSS-Fuzz-style symlink).
3. `--target=<name>` argument (removed from argv before libFuzzer parses it).

Unknown name, or none given: print `osl_fuzz: ...` message and the available
targets to stderr, exit 1.

## Pseudo-arguments

| Arg | Behavior |
|-----|----------|
| `--list-targets` | Print each registered target name on its own line (sorted) to stdout, flush, exit 0. |
| `--target=<name>` | See above. |

All other arguments pass through to libFuzzer unchanged.

## Environment

| Var | Effect |
|-----|--------|
| `OSL_FUZZ_TARGET` | Target selection. |
| `OSL_FUZZ_VERBOSE` | If set and non-empty/non-"0", OSL error/warning messages are printed instead of discarded. |

## `oso` target behavior

Input: arbitrary bytes interpreted as `.oso` text.

1. Load via `ShadingSystem::LoadMemoryCompiledShader("fuzz", bytes)`.
2. If loaded: build group `"fuzz"` with one layer of shader `"fuzz"`, usage
   `"surface"`.
3. If group built: `optimize_group(group, nullptr, do_jit=false)`.
4. Release group.

Guarantees on any input: returns normally unless a defect is hit; never
writes files; never reads files (beyond what OSL's init itself reads);
single-threaded; no cross-iteration accumulation of shader masters or groups.

Any crash, sanitizer report, abort, hang beyond `-timeout`, allocation beyond
`-malloc_limit_mb`, RSS beyond `-rss_limit_mb`, or per-input leak is a finding.

## Installed files

- `bin/osl_fuzz`
- `bin/osl_fuzz.options` (`[libfuzzer]` `max_len = 1048576`, `rss_limit_mb = 4096`)

## Build option

`OSL_BUILD_FUZZ_TARGETS` (default OFF; also read from env). Non-clang or
AppleClang: configure warning, targets skipped, rest of build unaffected.
With `SANITIZE` set and clang, OSL libraries also get
`-fsanitize=fuzzer-no-link`.
