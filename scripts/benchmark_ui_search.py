#!/usr/bin/env python3
"""Run and independently verify the opt-in synthetic QML search benchmark.

The default uses offscreen rendering. Native Windows requires an explicit
hosted or local opt-in. It does not enumerate applications, install hooks, or
use real keyboard input; a local run briefly displays a test window.
"""

import argparse
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess


def file_hash(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def validate_report(report, binary_hash, native=False, allow_legacy=False, execution_mode=None):
    expected = {"entries": 1000, "tiles": 100,
                "warmups": 5, "measuredQueries": 100, "releaseNdebug": True,
                "steadyClock": True, "binarySha256": binary_hash}
    if not isinstance(report, dict):
        raise ValueError("report must be an object")
    version = report.get("schemaVersion")
    if type(version) is not int or (version != 2 and not (allow_legacy and version == 1)):
        raise ValueError("incorrect schemaVersion")
    if execution_mode is not None and report.get("executionMode") != execution_mode:
        raise ValueError("incorrect executionMode")
    for key, value in expected.items():
        if type(report.get(key)) is not type(value) or report.get(key) != value:
            raise ValueError(f"incorrect {key}")
    if native:
        if report.get("qpa") != "windows" or report.get("graphicsApi") != "Direct3D11":
            raise ValueError("native evidence must use Windows QPA and Direct3D11 RHI")
    elif report.get("qpa") != "offscreen":
        raise ValueError("offscreen benchmark must use offscreen QPA")
    try:
        timestamp = datetime.fromisoformat(report.get("capturedAtUtc", "").replace("Z", "+00:00"))
    except (AttributeError, TypeError, ValueError) as error:
        raise ValueError("invalid capture timestamp") from error
    if timestamp.tzinfo is None:
        raise ValueError("capture timestamp lacks timezone")
    for key in ("scope", "boundary", "qtVersion", "compiler", "kernelVersion", "graphicsApi"):
        if not isinstance(report.get(key), str) or not report[key]:
            raise ValueError(f"missing {key}")
    samples = report.get("samples")
    if not isinstance(samples, list) or len(samples) != 100:
        raise ValueError("expected 100 raw samples")
    names = ["modelReady", "synchronizedFrame"]
    if version == 2:
        names += ["prefixInput", "inputReturn", "controllerReady", "finalKeyToController",
                  "finalKeyToRow", "controllerToRow"]
    times = {name: [] for name in names}
    for index, sample in enumerate(samples, 100):
        if not isinstance(sample, dict):
            raise ValueError("invalid sample")
        query = f"item{index:04d}"
        identity = f"c:/xdstarterbenchmark/{query}.lnk".encode("utf-8")
        app_id = "shortcut-" + hashlib.sha256(identity).hexdigest()
        if sample.get("query") != query or sample.get("expectedAppId") != app_id:
            raise ValueError("query or expected synthetic identity differs")
        for name in names:
            key = name + "Milliseconds"
            value = sample.get(key)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
                raise ValueError(f"invalid {key}")
            times[name].append(value)
        if times["synchronizedFrame"][-1] < times["modelReady"][-1]:
            raise ValueError("frame precedes model readiness")
        if version == 2:
            prefix = times["prefixInput"][-1]
            returned = times["inputReturn"][-1]
            controller = times["controllerReady"][-1]
            row = times["modelReady"][-1]
            if not (prefix <= returned <= row and prefix <= controller <= row):
                raise ValueError("invalid phase ordering")
            for name, actual in (("finalKeyToController", controller - prefix),
                                 ("finalKeyToRow", row - prefix),
                                 ("controllerToRow", row - controller)):
                if not math.isclose(times[name][-1], actual, rel_tol=0, abs_tol=1e-6):
                    raise ValueError(f"{name} disagrees with phase timestamps")
    for name, values in times.items():
        summary = report.get(name)
        if not isinstance(summary, dict):
            raise ValueError(f"missing {name} statistics")
        ordered = sorted(values)
        for key, actual in (("p50Milliseconds", ordered[49]),
                            ("p95Milliseconds", ordered[94]),
                            ("maximumMilliseconds", ordered[-1])):
            declared = summary.get(key)
            if (isinstance(declared, bool) or not isinstance(declared, (int, float)) or
                    not math.isfinite(declared) or not math.isclose(declared, actual, abs_tol=1e-6)):
                raise ValueError(f"{name} {key} disagrees with samples")


def benchmark_environment(base, mode, system):
    env = base.copy()
    env.pop("XDSTARTER_UI_BENCHMARK_LOCAL_NATIVE", None)
    hosted = env.get("GITHUB_ACTIONS") == "true" and env.get("RUNNER_ENVIRONMENT") == "github-hosted"
    if mode == "hosted-native" and not hosted:
        raise ValueError("--hosted-native requires a disposable GitHub-hosted runner")
    if mode == "local-native":
        if system != "nt" or hosted:
            raise ValueError("--local-native requires a non-hosted Windows session")
        env["XDSTARTER_UI_BENCHMARK_LOCAL_NATIVE"] = "1"
    if mode in ("hosted-native", "local-native"):
        env.update(QT_QPA_PLATFORM="windows", QSG_RHI_BACKEND="d3d11")
        env.pop("QT_QUICK_BACKEND", None)
    elif mode == "offscreen":
        env.update(QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
        if system == "nt" and env.get("WINDIR"):
            env["QT_QPA_FONTDIR"] = str(Path(env["WINDIR"]) / "Fonts")
    else:
        raise ValueError("unknown benchmark mode")
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--hosted-native", action="store_true")
    mode.add_argument("--local-native", action="store_true")
    parser.add_argument("--timeout-seconds", type=int, default=240)
    args = parser.parse_args()
    if not args.executable.is_file():
        parser.error("--executable must be a file")
    if args.output.exists() or not args.output.parent.is_dir():
        parser.error("--output must be a new file in an existing directory")
    if not 1 <= args.timeout_seconds <= 900:
        parser.error("--timeout-seconds must be in 1..900")
    execution_mode = "hosted-native" if args.hosted_native else "local-native" if args.local_native else "offscreen"
    try:
        env = benchmark_environment(os.environ, execution_mode, os.name)
    except ValueError as error:
        parser.error(str(error))
    env["XDSTARTER_UI_BENCHMARK_OUTPUT"] = str(args.output.resolve())
    env["XDSTARTER_UI_BENCHMARK_SAMPLES"] = "100"
    try:
        result = subprocess.run([str(args.executable.resolve()), "--ui-search-benchmark"],
                                env=env, timeout=args.timeout_seconds, check=False)
        if result.returncode != 0:
            raise ValueError(f"benchmark exited {result.returncode}")
        report = json.loads(args.output.read_text(encoding="utf-8"))
        validate_report(report, file_hash(args.executable), native=execution_mode != "offscreen",
                        execution_mode=execution_mode)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"UI benchmark failed: {error}\n")
    print(f"Validated 100 synthetic UI queries: {args.output}")


if __name__ == "__main__":
    main()
