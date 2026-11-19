#!/usr/bin/env bash

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Fuzz testing step for CI: optionally lints the fuzz corpus directory
# coverage, then (if OSL_FUZZ_TARGET is set) seeds, runs, and summarizes a
# libFuzzer session for that target. Modeled on OpenImageIO's
# src/build-scripts/ci-fuzztest.bash. Controlled by:
#   OSL_FUZZ_CORPUS_LINT   "true" to run the corpus coverage lint
#   OSL_FUZZ_TARGET        target name to fuzz (empty = skip fuzz run)
#   OSL_FUZZ_MAX_TIME      seconds for -max_total_time (default 3600)

set -e

FUZZ_BIN="$OSL_ROOT/bin/osl_fuzz"
# Inputs larger than this are not useful for .oso/.osl and slow mutation.
# Keep in sync with src/fuzz/osl_fuzz.options and populate_corpora.py.
MAX_LEN=1048576

#
# Verify every target reported by `osl_fuzz --list-targets` has a corpus
# directory in src/fuzz/corpora/.
#
if [[ "${OSL_FUZZ_CORPUS_LINT}" == "true" ]]; then
    lint_tmp=$(mktemp -d)
    ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0" \
        "$FUZZ_BIN" --list-targets | sort > "$lint_tmp/targets.txt"
    ls src/fuzz/corpora/ | sort > "$lint_tmp/corpus_dirs.txt"
    missing=$(comm -23 "$lint_tmp/targets.txt" "$lint_tmp/corpus_dirs.txt")
    if [[ -n "$missing" ]]; then
        echo "ERROR: Missing corpus director(ies) for fuzz target(s):"
        echo "$missing" | sed 's/^/  src\/fuzz\/corpora\//'
        echo ""
        echo "Add src/fuzz/corpora/<target>/ with at least a .gitkeep for each."
        exit 1
    fi
    count=$(wc -l < "$lint_tmp/targets.txt" | tr -d ' ')
    echo "OK: all ${count} fuzz targets have corpus directories."
fi

#
# Seed the corpus, run the fuzzer, and write a job summary for one target.
#
if [[ -n "${OSL_FUZZ_TARGET}" ]]; then
    if [[ ! -x "$FUZZ_BIN" ]]; then
        echo "::error::$FUZZ_BIN not found -- was OSL_BUILD_FUZZ_TARGETS=ON passed to cmake?"
        exit 1
    fi

    # corpus_dir persists across runs via the CI cache and accumulates the
    # coverage-increasing inputs libFuzzer discovers. seed_dir is gathered
    # fresh each run and then minimized into corpus_dir below.
    corpus_dir="corpus/${OSL_FUZZ_TARGET}"
    seed_dir="seeds/${OSL_FUZZ_TARGET}"
    mkdir -p "$corpus_dir" "$seed_dir"

    # Gather every available seed for this target (testsuite fixtures, and
    # for oso, every repo shader compiled with the just-built oslc) plus any
    # committed seeds. There is intentionally no cap on the number of seeds
    # -- the -merge step below keeps only the coverage-increasing subset.
    # The sanitizer-instrumented oslc may report benign leaks at exit; don't
    # let that count as a failed compile.
    ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0" \
        python3 src/fuzz/populate_corpora.py --target "$OSL_FUZZ_TARGET" \
            --dest seeds --oslc "$OSL_ROOT/bin/oslc"
    cp -rn "src/fuzz/corpora/${OSL_FUZZ_TARGET}/"* "$seed_dir/" 2>/dev/null || true

    skipped=0
    fuzz_status=0
    if ! ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0" \
         "$FUZZ_BIN" --list-targets | grep -qx "$OSL_FUZZ_TARGET"; then
        echo "::notice::Target '${OSL_FUZZ_TARGET}' not compiled in this build; skipping fuzz run."
        skipped=1
    else
        set +e
        # Corpus minimization: add only coverage-increasing seeds from seed_dir
        # into the (possibly cached) run corpus. A crash/hang here means a
        # seed or testsuite fixture itself trips OSL -- that is a finding,
        # surfaced via merge_status below. Leak checking is off for both
        # libFuzzer and LSan here -- a leaky seed is caught with a reproducer
        # by the timed run below, which replays the same corpus.
        ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0" \
        "$FUZZ_BIN" -merge=1 \
            -max_len=${MAX_LEN} \
            -rss_limit_mb=4096 \
            -malloc_limit_mb=2048 \
            -timeout=60 \
            -detect_leaks=0 \
            -artifact_prefix="crash_${OSL_FUZZ_TARGET}_" \
            "$corpus_dir" "$seed_dir"
        merge_status=$?
        # Timed, coverage-guided fuzzing over the minimized corpus.
        # Leak detection stays on: libFuzzer checks each input, so a leak stops
        # the run with a reproducer artifact rather than surfacing as an
        # unattributable LeakSanitizer report at process exit. Benign leaks in
        # dependencies are silenced via nosanitize.txt (LSAN_OPTIONS, set in
        # ci-startup.bash).
        "$FUZZ_BIN" "$corpus_dir" \
            -max_total_time="${OSL_FUZZ_MAX_TIME:-3600}" \
            -max_len=${MAX_LEN} \
            -rss_limit_mb=4096 \
            -malloc_limit_mb=2048 \
            -timeout=60 \
            -artifact_prefix="crash_${OSL_FUZZ_TARGET}_" \
            -jobs=$(nproc) -workers=$(nproc)
        fuzz_status=$?
        set -e
        # If minimization tripped a crash but the timed run was clean, still
        # fail the job on the merge-time finding.
        if [[ "$merge_status" -ne 0 && "$fuzz_status" -eq 0 ]]; then
            fuzz_status=$merge_status
        fi
    fi

    crash_count=$(find . -maxdepth 1 -name "crash_${OSL_FUZZ_TARGET}_*" | wc -l | tr -d ' ')
    if [[ "$fuzz_status" -ne 0 ]]; then
        {
            echo "Target: ${OSL_FUZZ_TARGET}"
            echo "Status: FAILED"
            echo "Detail: fuzzer ran and found a problem (crash/timeout/OOM/leak); see uploaded crash artifacts"
            echo "Crash artifacts found: ${crash_count}"
            echo ""
        } >> "$GITHUB_STEP_SUMMARY"
    elif [[ "$skipped" -eq 1 ]]; then
        {
            echo "Target: ${OSL_FUZZ_TARGET}"
            echo "Status: SKIPPED"
            echo "Detail: fuzzing was not run because this target is not compiled into this build"
            echo ""
        } >> "$GITHUB_STEP_SUMMARY"
    fi

    exit "$fuzz_status"
fi
