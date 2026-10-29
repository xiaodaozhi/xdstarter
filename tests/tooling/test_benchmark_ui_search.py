"""Check evidence integrity independently of the synthetic GUI process."""

from copy import deepcopy
import hashlib
from pathlib import Path
import runpy
import unittest


API = runpy.run_path(str(Path(__file__).resolve().parents[2] / "scripts/benchmark_ui_search.py"))
VALIDATE = API["validate_report"]


def fixture():
    samples = []
    for index in range(100, 200):
        query = f"item{index:04d}"
        identity = f"c:/xdstarterbenchmark/{query}.lnk".encode()
        samples.append({"query": query, "expectedAppId": "shortcut-" + hashlib.sha256(identity).hexdigest(),
                        "modelReadyMilliseconds": float(index - 99),
                        "synchronizedFrameMilliseconds": float(index - 98)})
    return {"schemaVersion": 1, "capturedAtUtc": "2026-10-29T00:00:00Z", "entries": 1000, "tiles": 100, "warmups": 5,
            "measuredQueries": 100, "releaseNdebug": True, "steadyClock": True,
            "binarySha256": "a" * 64, "qpa": "offscreen", "graphicsApi": "Software",
            "qtVersion": "6.8.2", "compiler": "fixture", "kernelVersion": "fixture",
            "scope": "fixture", "boundary": "fixture", "samples": samples,
            "modelReady": {"p50Milliseconds": 50.0, "p95Milliseconds": 95.0,
                           "maximumMilliseconds": 100.0},
            "synchronizedFrame": {"p50Milliseconds": 51.0, "p95Milliseconds": 96.0,
                                   "maximumMilliseconds": 101.0}}


class UiSearchBenchmarkEvidenceTests(unittest.TestCase):
    def test_complete_evidence_and_explicit_native_mode(self):
        report = fixture()
        VALIDATE(report, "a" * 64)
        report.update(qpa="windows", graphicsApi="Direct3D11")
        VALIDATE(report, "a" * 64, native=True)

    def test_incomplete_inconsistent_or_invalid_evidence_is_rejected(self):
        for change in ("missing", "query", "identity", "nan", "ordering", "summary",
                       "binary", "debug", "count", "timestamp", "wrong_native_backend"):
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
            else:
                native = True
                report.update(qpa="windows", graphicsApi="Software")
            with self.subTest(change=change), self.assertRaises(ValueError):
                VALIDATE(report, "a" * 64, native=native)


if __name__ == "__main__":
    unittest.main()
