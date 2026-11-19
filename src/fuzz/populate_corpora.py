#!/usr/bin/env python3
# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage
"""
Gather fuzz seed files for each osl_fuzz target from its configured sources.

Modeled on OpenImageIO's src/fuzz/populate_corpora.py, with fuzz "targets"
(OSL input types) in place of image formats.

For every target, TARGET_SOURCES lists where its seeds come from. A source
either copies matching files from the repo, or compiles shader source with
oslc and keeps the resulting .oso. Every file up to the per-file size cap
(MAX_BYTES) is gathered -- there is no cap on seed count. Coverage-based
minimization happens downstream in ci-fuzztest.bash via `osl_fuzz -merge=1`,
which keeps only the coverage-increasing subset, so it is fine (and intended)
to feed in everything.

Run from the repository root. In CI this is invoked with --dest to collect
seeds into a scratch dir for minimization; without --dest it writes into
src/fuzz/corpora/<target>/ (which is otherwise empty except for .gitkeep).

Usage:
    python src/fuzz/populate_corpora.py [--target <name>] [--dest DIR]
                                        [--oslc PATH] [--dry-run]
"""

import argparse
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CORPUS_ROOT = REPO_ROOT / "src" / "fuzz" / "corpora"
# Per-file size cap only; there is deliberately no cap on the *number* of
# seeds. Matches the -max_len passed to osl_fuzz (osl_fuzz.options,
# ci-fuzztest.bash). Real .oso files are tens of KB, so this is generous.
MAX_BYTES = 1024 * 1024  # 1 MB per file


# Source map: target -> list of (kind, repo-relative root dir, [extensions]).
# Roots are searched recursively.
#   kind "copy"        : copy matching files as-is
#   kind "compile_osl" : compile matching .osl files with oslc, keep the .oso
# Hand-written .oso fixtures in testsuite/ (e.g. loader regression tests such
# as testsuite/oso-excess-defaults/) are picked up by the "copy" source, so
# regression inputs for fixed bugs automatically become seeds.
TARGET_SOURCES = {
    "oso": [
        ("copy",        "testsuite",   ["oso"]),
        ("compile_osl", "testsuite",   ["osl"]),
        ("compile_osl", "src/shaders", ["osl"]),
    ],
    # Future targets (not yet implemented in osl_fuzz):
    # "osl": [
    #     ("copy", "testsuite",   ["osl"]),
    #     ("copy", "src/shaders", ["osl"]),
    # ],
    # "scene": [
    #     ("copy", "testsuite", ["xml"]),
    # ],
}


def find_oslc(explicit):
    """Locate oslc: --oslc, then $OSL_ROOT/bin, dist/bin, build/bin."""
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("OSL_ROOT"):
        candidates.append(Path(os.environ["OSL_ROOT"]) / "bin" / "oslc")
    candidates.append(REPO_ROOT / "dist" / "bin" / "oslc")
    candidates.append(REPO_ROOT / "build" / "bin" / "oslc")
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            # Absolute, because compiles run with cwd set to each shader's dir.
            return c.resolve()
    return None


def candidate_files(root, exts):
    """All non-empty files under repo-relative root with one of exts."""
    d = REPO_ROOT / root
    if not d.is_dir():
        return []
    exts = [e.lower() for e in exts]
    result = []
    for f in sorted(d.rglob("*")):
        if f.is_file() and f.suffix.lstrip(".").lower() in exts:
            try:
                if f.stat().st_size > 0:
                    result.append(f)
            except OSError:
                pass
    return result


def seed_name(src, ext):
    """Unique flat seed name from the repo-relative path: many shaders are
    named test.osl, so the directory has to be part of the name."""
    rel = src.relative_to(REPO_ROOT).with_suffix("")
    return "_".join(rel.parts) + "." + ext


def compile_one(oslc, src, dst):
    """Compile src to dst. Return True if dst was produced."""
    cmd = [str(oslc), "-q", "-I" + str(src.parent), "-o", str(dst), str(src)]
    try:
        r = subprocess.run(cmd, cwd=src.parent, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=120)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return r.returncode == 0 and dst.is_file()


def populate_target(target, dest, oslc, dry_run):
    """
    Gather all seeds for target into dest.
    Returns dict of counts: copied, compiled, compile_failed, too_big, skipped.
    """
    counts = dict(copied=0, compiled=0, compile_failed=0, too_big=0,
                  skipped=0)
    jobs = []
    for kind, root, exts in TARGET_SOURCES[target]:
        for src in candidate_files(root, exts):
            if kind == "copy":
                dst = dest / seed_name(src, src.suffix.lstrip("."))
                if dst.exists():
                    counts["skipped"] += 1
                elif src.stat().st_size > MAX_BYTES:
                    counts["too_big"] += 1
                else:
                    if not dry_run:
                        shutil.copy2(src, dst)
                    counts["copied"] += 1
            elif kind == "compile_osl":
                dst = dest / seed_name(src, "oso")
                if dst.exists():
                    counts["skipped"] += 1
                else:
                    jobs.append((src, dst))
            else:
                raise ValueError(f"unknown source kind {kind!r}")

    if jobs and dry_run:
        counts["compiled"] += len(jobs)  # would attempt
    elif jobs:
        if oslc is None:
            print(f"  {target}: ERROR: oslc not found; use --oslc or set "
                  "OSL_ROOT", file=sys.stderr)
            counts["compile_failed"] += len(jobs)
        else:
            with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
                results = list(ex.map(lambda j: compile_one(oslc, *j), jobs))
            for (src, dst), ok in zip(jobs, results):
                if not ok:
                    counts["compile_failed"] += 1
                elif dst.stat().st_size > MAX_BYTES:
                    dst.unlink()
                    counts["too_big"] += 1
                else:
                    counts["compiled"] += 1
    return counts


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", metavar="NAME",
                        help="Populate only this target (default: all)")
    parser.add_argument("--dest", metavar="DIR", type=Path,
                        help="Write seeds into DIR/<target>/ instead of "
                             "src/fuzz/corpora/<target>/")
    parser.add_argument("--oslc", metavar="PATH",
                        help="oslc to compile shader seeds with (default: "
                             "$OSL_ROOT/bin/oslc, dist/bin/oslc, "
                             "build/bin/oslc)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show what would be gathered without writing")
    args = parser.parse_args()

    if args.target and args.target not in TARGET_SOURCES:
        print(f"Unknown target {args.target!r}; known: "
              + ", ".join(sorted(TARGET_SOURCES)), file=sys.stderr)
        return 1
    targets = [args.target] if args.target else sorted(TARGET_SOURCES)
    oslc = find_oslc(args.oslc)
    prefix = "[dry-run] " if args.dry_run else ""
    if oslc:
        print(f"{prefix}Using oslc: {oslc}")

    status = 0
    for target in targets:
        dest = ((args.dest / target) if args.dest
                else (CORPUS_ROOT / target)).resolve()
        if not args.dry_run:
            dest.mkdir(parents=True, exist_ok=True)
        c = populate_target(target, dest, oslc, args.dry_run)
        total = c["copied"] + c["compiled"]
        print(f"  {prefix}{target}: +{total} seeds "
              f"({c['copied']} copied, {c['compiled']} compiled, "
              f"{c['skipped']} already present, "
              f"{c['compile_failed']} failed to compile, "
              f"{c['too_big']} over {MAX_BYTES // 1024} KB)")
        if total == 0 and c["skipped"] == 0:
            print(f"  {target}: WARNING: no seeds gathered", file=sys.stderr)
            status = 1
    # Some testsuite shaders are deliberately invalid (they test compiler
    # errors), so a nonzero compile-failure count is expected and not fatal.
    return status


if __name__ == "__main__":
    sys.exit(main())
