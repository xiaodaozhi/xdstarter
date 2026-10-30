"""Check evidence integrity independently of the synthetic GUI process."""

from copy import deepcopy
import hashlib
from pathlib import Path
import runpy
import unittest


API = runpy.run_path(str(Path(__file__).resolve().parents[2] / "scripts/benchmark_ui_search.py"))
VALIDATE = API["validate_report"]
ENVIRONMENT = API["benchmark_environment"]


def fixture():
    samples = []
    for index in range(100, 200):
        query = f"item{index:04d}"
        identity = f"c:/xdstarterbenchmark/{query}.lnk".encode()
        elapsed = float(index - 99)
        samples.append({"query": query, "expectedAppId": "shortcut-" + hashlib.sha256(identity).hexdigest(),
                        "prefixInputMilliseconds": elapsed * 0.2,
                        "inputReturnMilliseconds": elapsed * 0.4,
                        "controllerReadyMilliseconds": elapsed * 0.6,
                        "finalKeyToControllerMilliseconds": elapsed * 0.4,
                        "finalKeyToRowMilliseconds": elapsed * 0.8,
                        "controllerToRowMilliseconds": elapsed * 0.4,
                        "modelReadyMilliseconds": elapsed,
                        "synchronizedFrameMilliseconds": elapsed + 1})
    report = {"schemaVersion": 2, "capturedAtUtc": "2026-10-29T00:00:00Z", "entries": 1000, "tiles": 100, "warmups": 5,
            "measuredQueries": 100, "releaseNdebug": True, "steadyClock": True,
            "binarySha256": "a" * 64, "qpa": "offscreen", "graphicsApi": "Software",
            "qtVersion": "6.8.2", "compiler": "fixture", "kernelVersion": "fixture",
            "scope": "fixture", "boundary": "fixture", "samples": samples,
            "modelReady": {"p50Milliseconds": 50.0, "p95Milliseconds": 95.0,
                           "maximumMilliseconds": 100.0},
            "synchronizedFrame": {"p50Milliseconds": 51.0, "p95Milliseconds": 96.0,
                                   "maximumMilliseconds": 101.0}}
    for name, fraction in (("prefixInput", .2), ("inputReturn", .4), ("controllerReady", .6),
                           ("finalKeyToController", .4), ("finalKeyToRow", .8),
                           ("controllerToRow", .4)):
        report[name] = {"p50Milliseconds": 50.0 * fraction, "p95Milliseconds": 95.0 * fraction,
                        "maximumMilliseconds": 100.0 * fraction}
    return report


class UiSearchBenchmarkEvidenceTests(unittest.TestCase):
    def test_complete_evidence_and_explicit_native_mode(self):
        report = fixture()
        VALIDATE(report, "a" * 64)
        report.update(qpa="windows", graphicsApi="Direct3D11")
        VALIDATE(report, "a" * 64, native=True)

    def test_run_identity_is_verified_when_required(self):
        report = fixture()
        report.update(qpa="windows", graphicsApi="Direct3D11", executionMode="local-native")
        VALIDATE(report, "a" * 64, native=True, execution_mode="local-native")
        with self.assertRaisesRegex(ValueError, "executionMode"):
            VALIDATE(report, "a" * 64, native=True, execution_mode="hosted-native")
        del report["executionMode"]
        with self.assertRaisesRegex(ValueError, "executionMode"):
            VALIDATE(report, "a" * 64, native=True, execution_mode="local-native")

    def test_native_opt_in_is_explicit_and_hosted_guard_is_preserved(self):
        inherited = {"XDSTARTER_UI_BENCHMARK_LOCAL_NATIVE": "1", "QT_QUICK_BACKEND": "software"}
        offscreen = ENVIRONMENT(inherited, "offscreen", "nt")
        self.assertNotIn("XDSTARTER_UI_BENCHMARK_LOCAL_NATIVE", offscreen)
        self.assertEqual(offscreen["QT_QPA_PLATFORM"], "offscreen")
        local = ENVIRONMENT({}, "local-native", "nt")
        self.assertEqual(local["XDSTARTER_UI_BENCHMARK_LOCAL_NATIVE"], "1")
        self.assertEqual(local["QT_QPA_PLATFORM"], "windows")
        self.assertEqual(local["QSG_RHI_BACKEND"], "d3d11")
        self.assertNotIn("QT_QUICK_BACKEND", local)
        hosted_markers = {"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "github-hosted"}
        hosted = ENVIRONMENT(hosted_markers, "hosted-native", "nt")
        self.assertNotIn("XDSTARTER_UI_BENCHMARK_LOCAL_NATIVE", hosted)
        for env, mode, system in (({}, "local-native", "posix"),
                                  (hosted_markers, "local-native", "nt"),
                                  ({}, "hosted-native", "nt"),
                                  ({"GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "self-hosted"},
                                   "hosted-native", "nt"),
                                  ({}, "other", "nt")):
            with self.subTest(env=env, mode=mode, system=system), self.assertRaises(ValueError):
                ENVIRONMENT(env, mode, system)

    def test_incomplete_inconsistent_or_invalid_evidence_is_rejected(self):
        for change in ("missing", "query", "identity", "nan", "ordering", "summary",
                       "binary", "debug", "count", "timestamp", "wrong_native_backend",
                       "phase_missing", "phase_order", "phase_difference", "phase_summary"):
            report = deepcopy(fixture())
            native = False
            if change == "missing":
                report["samples"].pop()
            elif change == "query":
                report["samples"][1]["query"] = report["samples"][0]["query"]
            elif change == "identity":
                report["samples"][0]["expectedAppId"] = "wrong"
            elif change == "nan":
                report["samples"][0]["modelReadyMilliseconds"] = float("nan")
            elif change == "ordering":
                report["samples"][0]["synchronizedFrameMilliseconds"] = 0
            elif change == "summary":
                report["synchronizedFrame"]["p95Milliseconds"] += 1
            elif change == "binary":
                report["binarySha256"] = "b" * 64
            elif change == "debug":
                report["releaseNdebug"] = False
            elif change == "count":
                report["schemaVersion"] = True
            elif change == "timestamp":
                report["capturedAtUtc"] = "not-a-timestamp"
            elif change == "phase_missing":
                del report["samples"][0]["controllerReadyMilliseconds"]
            elif change == "phase_order":
                report["samples"][0]["inputReturnMilliseconds"] = 2
            elif change == "phase_difference":
                report["samples"][0]["finalKeyToRowMilliseconds"] += 0.1
            elif change == "phase_summary":
                report["controllerToRow"]["p95Milliseconds"] += 1
            else:
                native = True
                report.update(qpa="windows", graphicsApi="Software")
            with self.subTest(change=change), self.assertRaises(ValueError):
                VALIDATE(report, "a" * 64, native=native)

    def test_legacy_requires_explicit_opt_in(self):
        report = fixture()
        report["schemaVersion"] = 1
        with self.assertRaises(ValueError):
            VALIDATE(report, "a" * 64)
        VALIDATE(report, "a" * 64, allow_legacy=True)


if __name__ == "__main__":
    unittest.main()
