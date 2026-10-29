#!/usr/bin/env python3
"""Collect independently checked, synthetic search-service timings for #35.

The executable is a QCoreApplication test fixture; this does not launch the
product, render QML, enumerate real applications, or install input hooks.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys


SAMPLE_COUNT = 100
MAX_ENTRIES = 8192
DATASET = "synthetic-offline-applications-v1"
QUERY_SET = "ten-families-ten-numbers-v2"


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_report(report, entry_count, expected_hash):
    """Reject incomplete or inconsistent measurements before saving them."""
    if not isinstance(report, dict):
        raise ValueError("benchmark output must be an object")
    expected = {
        "reportVersion": 2,
        "dataset": DATASET,
        "querySet": QUERY_SET,
        "entries": entry_count,
        "binarySha256": expected_hash,
        "measuredQueries": SAMPLE_COUNT,
        "distinctMeasuredQueries": SAMPLE_COUNT,
        "monotonicClock": True,
        "releaseNdebug": True,
        "perQueryResultsIdentical": True,
    }
    for key, value in expected.items():
        if report.get(key) != value or (value is True and report.get(key) is not True):
            raise ValueError(f"benchmark {key} does not match the expected run")
    samples = report.get("samples")
    if not isinstance(samples, list) or len(samples) != SAMPLE_COUNT:
        raise ValueError("benchmark must contain exactly 100 raw samples")
    queries = []
    direct = []
    service = []
    counts = []
    for sample in samples:
        if not isinstance(sample, dict) or not isinstance(sample.get("query"), str) or not sample["query"]:
            raise ValueError("invalid benchmark query")
        queries.append(sample["query"])
        for key, target in (("directMilliseconds", direct), ("serviceMilliseconds", service)):
            duration = sample.get(key)
            if isinstance(duration, bool) or not isinstance(duration, (int, float)) or not math.isfinite(duration) or duration < 0:
                raise ValueError(f"invalid {key} duration")
            target.append(duration)
        count = sample.get("matchCount")
        if isinstance(count, bool) or not isinstance(count, int) or count < 0:
            raise ValueError("invalid match count")
        counts.append(count)
    if len(set(queries)) != SAMPLE_COUNT:
        raise ValueError("benchmark queries must be distinct")
    for values, key in ((direct, "directLookup"), (service, "serviceRoundTrip")):
        summary = report.get(key)
        if not isinstance(summary, dict):
            raise ValueError(f"missing {key} summary")
        ordered = sorted(values)
        for field, actual in (("p50Milliseconds", ordered[49]),
                              ("p95Milliseconds", ordered[94]),
                              ("maximumMilliseconds", ordered[-1])):
            declared = summary.get(field)
            if (isinstance(declared, bool) or not isinstance(declared, (int, float)) or
                    not math.isfinite(declared) or not math.isclose(declared, actual, abs_tol=1e-6)):
                raise ValueError(f"{key} {field} disagrees with raw samples")
        if summary.get("matchCountChecksum") != sum(counts):
            raise ValueError(f"{key} checksum disagrees with raw samples")
    if report.get("matchCountChecksum") != sum(counts):
        raise ValueError("top-level checksum disagrees with raw samples")
    for key in ("buildMilliseconds", "serviceIndexReadyMilliseconds"):
        duration = report.get(key)
        if isinstance(duration, bool) or not isinstance(duration, (int, float)) or not math.isfinite(duration) or duration < 0:
            raise ValueError(f"invalid {key}")
    for old_field, new_field in (("searchP50Milliseconds", "p50Milliseconds"),
                                 ("searchP95Milliseconds", "p95Milliseconds"),
                                 ("searchMaximumMilliseconds", "maximumMilliseconds")):
        if report.get(old_field) != report["directLookup"][new_field]:
            raise ValueError(f"{old_field} disagrees with direct summary")
    for key in ("qtBuildVersion", "qtRuntimeVersion", "compiler", "buildAbi", "kernelVersion"):
        if not isinstance(report.get(key), str) or not report[key]:
            raise ValueError(f"missing {key}")


def collect(executable, entry_counts, timeout_seconds):
    expected_hash = sha256_file(executable)
    reports = []
    for count in entry_counts:
        result = subprocess.run(
            [str(executable), "--search-benchmark", "--entries", str(count)],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            timeout=timeout_seconds, check=False,
        )
        if result.returncode != 0:
            raise ValueError(f"benchmark for {count} entries failed ({result.returncode}): {result.stderr.strip()}")
        try:
            report = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise ValueError(f"benchmark for {count} entries did not produce valid JSON") from error
        validate_report(report, count, expected_hash)
        reports.append(report)
    return {
        "schemaVersion": 1,
        "capturedAtUtc": datetime.now(timezone.utc).isoformat(),
        "binarySha256": expected_hash,
        "scope": "synthetic search service; index ready; no QML, real catalog or native input",
        "percentileMethod": "nearest rank; 100 samples; p50=50th and p95=95th",
        "cases": reports,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True, help="Release search test executable")
    parser.add_argument("--output", type=Path, required=True, help="new JSON output file; never overwritten")
    parser.add_argument("--entries", type=int, nargs="+", default=[0, 1000, 4096])
    parser.add_argument("--timeout-seconds", type=int, default=120, help="timeout per corpus size")
    args = parser.parse_args(argv)
    if not args.executable.is_file():
        parser.error("--executable must be an existing file")
    if args.output.exists():
        parser.error("--output already exists")
    if (not args.entries or len(set(args.entries)) != len(args.entries) or
            any(count < 0 or count > MAX_ENTRIES for count in args.entries)):
        parser.error(f"--entries must contain distinct values in 0..{MAX_ENTRIES}")
    if not 1 <= args.timeout_seconds <= 900:
        parser.error("--timeout-seconds must be in 1..900")
    try:
        evidence = collect(args.executable.resolve(), args.entries, args.timeout_seconds)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8", newline="\n") as output:
            json.dump(evidence, output, ensure_ascii=False, indent=2, allow_nan=False)
            output.write("\n")
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"Search benchmark failed: {error}", file=sys.stderr)
        return 1
    print(f"Saved {len(evidence['cases'])} validated search cases to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
