# Quickstart: validating OSL fuzzing

## Prerequisites

- Upstream clang with libFuzzer (Linux distro clang, or `brew install llvm`
  on macOS; AppleClang will not work).
- OSL's normal dependencies (LLVM, OIIO, ...).

## 1. Configure option is inert by default

```bash
cmake -B build -S .            # no fuzz option
grep OSL_BUILD_FUZZ_TARGETS build/CMakeCache.txt   # -> OFF
```

Expected: no `src/fuzz` targets; normal build/testsuite unchanged.

## 2. AppleClang / gcc are skipped gracefully

Configure with `-DOSL_BUILD_FUZZ_TARGETS=ON` using AppleClang or gcc.
Expected: a CMake warning naming the reason; configure succeeds; no `osl_fuzz`.

## 3. Build the harness

```bash
cmake -B build-fuzz -S . \
    -DCMAKE_C_COMPILER=$(brew --prefix llvm)/bin/clang \
    -DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++ \
    -DCMAKE_BUILD_TYPE=Release \
    -DOSL_BUILD_FUZZ_TARGETS=ON -DSANITIZE=address,undefined \
    -DOSL_BUILD_TESTS=OFF -DUSE_PYTHON=OFF \
    -DCMAKE_INSTALL_PREFIX=$PWD/dist-fuzz
cmake --build build-fuzz --target install
```

Expected: `dist-fuzz/bin/osl_fuzz` and `dist-fuzz/bin/osl_fuzz.options`.

## 4. Dispatch contract

```bash
dist-fuzz/bin/osl_fuzz --list-targets           # -> "oso", exit 0
dist-fuzz/bin/osl_fuzz; echo $?                 # -> lists targets, 1
dist-fuzz/bin/osl_fuzz --target=bogus; echo $?  # -> error + list, 1
```

See [contracts/harness-contract.md](contracts/harness-contract.md).

## 5. Gather seeds and run a seeds-only pass (SC-002)

```bash
python3 src/fuzz/populate_corpora.py --target oso --dest /tmp/osl-seeds \
    --oslc dist-fuzz/bin/oslc
OSL_FUZZ_TARGET=oso dist-fuzz/bin/osl_fuzz /tmp/osl-seeds/oso -runs=0
```

Expected: script reports N copied/compiled and M expected compile failures,
N >= 90% of compilable shaders (SC-007); harness processes all seeds with no
crash.

## 6. Timed run (SC-001)

```bash
mkdir -p /tmp/osl-corpus
OSL_FUZZ_TARGET=oso dist-fuzz/bin/osl_fuzz /tmp/osl-corpus /tmp/osl-seeds/oso \
    -max_total_time=60 -max_len=1048576 -rss_limit_mb=4096 -malloc_limit_mb=2048 -timeout=60
```

Expected: runs 60 s, coverage (`cov:`) grows, exits 0. Any crash leaves a
`crash-*` file.

## 7. Known-bad reproducer (US1 independent test)

Revert the fix from #2220 (`git revert --no-commit c38f3a32`) in a scratch
build, rebuild, then:

```bash
OSL_FUZZ_TARGET=oso dist-fuzz/bin/osl_fuzz testsuite/oso-excess-defaults/param_float_many.oso
```

Expected: ASan heap-buffer-overflow report, non-zero exit. With the fix in
place: clean exit. `OSL_FUZZ_VERBOSE=1` shows OSL's load error.

## 8. CI (stage 2)

Push to a branch whose name contains `fuzz`. Expected: `Fuzz oso (tier 1)`
and `Fuzz corpus coverage lint` jobs run; lint passes; fuzz job runs for its
budget and passes (or fails with a `fuzz-crashes-oso-<run_id>` artifact). See
[contracts/ci-contract.md](contracts/ci-contract.md).
