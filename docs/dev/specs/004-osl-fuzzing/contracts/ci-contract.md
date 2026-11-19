# Contract: Fuzz CI

## `src/build-scripts/ci-fuzztest.bash`

Inputs (env):

| Var | Meaning |
|-----|---------|
| `OSL_FUZZ_CORPUS_LINT` | `true` -> verify every `osl_fuzz --list-targets` entry has `src/fuzz/corpora/<target>/`; fail listing missing ones. |
| `OSL_FUZZ_TARGET` | Target to fuzz; empty -> skip fuzz run. |
| `OSL_FUZZ_MAX_TIME` | `-max_total_time` seconds (default 3600). |
| `OSL_ROOT` | Install prefix; binary is `$OSL_ROOT/bin/osl_fuzz`. |

Sequence when `OSL_FUZZ_TARGET` set:
1. `python3 src/fuzz/populate_corpora.py --target $T --dest seeds` (+ copy committed `src/fuzz/corpora/$T/*`).
2. If target not in `--list-targets`: SKIPPED summary, exit 0.
3. Merge: `osl_fuzz -merge=1 corpus/$T seeds/$T` (leaks off), limits as below.
4. Timed run: `osl_fuzz corpus/$T -max_total_time=... -jobs=$(nproc) -workers=$(nproc)`.
5. Fail if either step failed; write summary to `$GITHUB_STEP_SUMMARY`.

Limits: `-max_len=1048576 -rss_limit_mb=4096 -malloc_limit_mb=2048 -timeout=60 -artifact_prefix=crash_${T}_`.

## `.github/workflows/build-steps.yml` additions

Inputs: `fuzz_target` (string, ''), `fuzz_max_time` (string, '3600'),
`fuzz_corpus_lint` (boolean, false).

Steps after Build, all conditional on those inputs: restore corpus cache ->
Fuzz (runs `ci-fuzztest.bash`) -> save corpus cache (always) -> upload
`crash_<target>_*` as `fuzz-crashes-<target>-<run_id>` on failure.

## `.github/workflows/fuzz.yml`

- Triggers: nightly cron (canonical repo only), push and PR on `*fuzz*`
  branches (path-filtered to `src/**` minus docs, and the two workflow
  files), `workflow_dispatch` with optional `target` input.
- `fail-fast: false` matrix; this feature: `{ target: oso, tier: 1, max_total_time: 3600 }`.
- `fuzz-corpus-lint` job.
- Adding a target = one matrix line.
