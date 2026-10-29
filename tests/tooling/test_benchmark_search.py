"""Validate the search evidence boundary without starting a GUI or hooks."""

from copy import deepcopy
from pathlib import Path
import runpy
import unittest


API = runpy.run_path(str(Path(__file__).resolve().parents[2] / "scripts/benchmark_search.py"))
VALIDATE = API["validate_report"]


def fixture():
    samples = [{"query": f"fixture-{index}", "directMilliseconds": index + 1.0,
                "serviceMilliseconds": index + 2.0, "matchCount": index % 3}
               for index in range(100)]
    checksum = sum(sample["matchCount"] for sample in samples)
    return {"reportVersion": 2, "dataset": API["DATASET"], "querySet": API["QUERY_SET"],
            "entries": 1000, "binarySha256": "a" * 64, "measuredQueries": 100,
            "distinctMeasuredQueries": 100, "monotonicClock": True,
            "releaseNdebug": True, "perQueryResultsIdentical": True, "samples": samples,
            "qtBuildVersion": "6.8.2", "qtRuntimeVersion": "6.8.2",
            "compiler": "fixture", "buildAbi": "fixture", "kernelVersion": "fixture",
            "matchCountChecksum": checksum, "buildMilliseconds": 1.0,
            "serviceIndexReadyMilliseconds": 2.0, "searchP50Milliseconds": 50.0,
            "searchP95Milliseconds": 95.0, "searchMaximumMilliseconds": 100.0,
            "directLookup": {"p50Milliseconds": 50.0, "p95Milliseconds": 95.0,
                             "maximumMilliseconds": 100.0, "matchCountChecksum": checksum},
            "serviceRoundTrip": {"p50Milliseconds": 51.0, "p95Milliseconds": 96.0,
                                 "maximumMilliseconds": 101.0, "matchCountChecksum": checksum}}


class SearchBenchmarkEvidenceTests(unittest.TestCase):
    def test_complete_raw_evidence_matches_its_summary(self):
        VALIDATE(fixture(), 1000, "a" * 64)

    def test_missing_duplicate_or_nonfinite_samples_are_rejected(self):
        for change in ("missing", "duplicate", "nan"):
            report = fixture()
            if change == "missing":
                report["samples"].pop()
            elif change == "duplicate":
                report["samples"][1]["query"] = report["samples"][0]["query"]
            else:
                report["samples"][0]["serviceMilliseconds"] = float("nan")
            with self.subTest(change=change), self.assertRaises(ValueError):
                VALIDATE(report, 1000, "a" * 64)

    def test_wrong_summary_binary_or_result_agreement_is_rejected(self):
        original = fixture()
        for change in ("percentile", "checksum", "binary", "results", "debug", "build_time"):
            report = deepcopy(original)
            if change == "percentile":
                report["serviceRoundTrip"]["p95Milliseconds"] = 95.0
            elif change == "checksum":
                report["directLookup"]["matchCountChecksum"] += 1
            elif change == "binary":
                report["binarySha256"] = "b" * 64
            elif change == "results":
                report["perQueryResultsIdentical"] = False
            elif change == "debug":
                report["releaseNdebug"] = False
            else:
                report["buildMilliseconds"] = -1
            with self.subTest(change=change), self.assertRaises(ValueError):
                VALIDATE(report, 1000, "a" * 64)


if __name__ == "__main__":
    unittest.main()
