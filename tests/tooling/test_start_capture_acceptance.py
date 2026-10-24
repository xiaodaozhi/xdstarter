# tests/tooling/test_start_capture_acceptance.py

"""Offline acceptance evidence validation; no hooks, native input or windows."""

from copy import deepcopy
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import runpy
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


API = runpy.run_path(str(Path(__file__).resolve().parents[2] / "scripts/start_capture_acceptance.py"))


def fixture():
    header = {"kind": "run", "runId": "fixture-run", "mode": "area-cache",
              "executableSha256": "a" * 64, "clock": "fixture milliseconds"}
    events = [header,
              {"kind": "input", "sequence": 1, "at": 120, "event": "left-down",
               "originSessionGeneration": 3, "gestureId": 7, "injected": False},
              {"kind": "input", "sequence": 2, "at": 150, "event": "left-up",
               "originSessionGeneration": 3, "gestureId": 7, "consumed": True,
               "toggle": True, "nativeDispatchQueued": True},
              {"kind": "host", "at": 200, "event": "presentation-result",
               "sessionGeneration": 3, "gestureId": 7, "requestedAction": "show",
               "nativeMeasured": True, "visible": True, "minimized": False,
               "foreground": True, "releasedToObservationMs": 50, "releasedAt": 150,
               "pressedAt": 120},
              {"kind": "end"}]
    annotations = {key: header[key] for key in ("runId", "mode", "executableSha256")}
    annotations.update(schemaVersion=1, inputSource="posted-fixture", outcomeSource="fixture",
                       trials=[{"trialId": "one", "startAt": 100, "endAt": 300,
                                "expectedAction": "show", "intent": "start",
                                "observed": "xdstarter-only"}])
    return events, annotations


class CaptureAcceptanceTests(unittest.TestCase):
    def report(self, events, annotations):
        return API["make_report"](events, annotations)

    def test_unique_native_show_has_correlated_latency(self):
        events, annotations = fixture()
        result = self.report(events, annotations)
        self.assertTrue(result["trials"][0]["success"])
        self.assertEqual(result["latency"]["p95Ms"], 50)
        self.assertEqual(result["inputSource"], "posted-fixture")
        self.assertEqual(result["outcomeSource"], "fixture")

    def test_no_native_measurement_is_not_foreground_evidence(self):
        events, annotations = fixture()
        events[-2]["nativeMeasured"] = False
        self.assertEqual(self.report(events, annotations)["latency"]["samples"], 0)

    def test_unknown_outcomes_do_not_enter_success_denominator(self):
        events, annotations = fixture()
        annotations["trials"][0]["observed"] = "unknown"
        result = self.report(events, annotations)
        self.assertEqual(result["annotatedStartTrials"], 0)
        self.assertIsNone(result["trials"][0]["success"])
        self.assertEqual(result["unknownTrials"], 1)

    def test_missing_callback_does_not_erase_independently_observed_failure(self):
        events, annotations = fixture()
        events = [events[0], events[-1]]
        annotations["trials"][0]["observed"] = "native-only"
        result = self.report(events, annotations)
        self.assertEqual(result["observedStartOutcomes"], {"native-only": 1})
        self.assertEqual(result["unobservedStartIntentTrials"], 1)
        self.assertFalse(result["trials"][0]["success"])
        self.assertEqual(result["latency"]["samples"], 0)

    def test_double_menu_never_counts_as_successful_show_latency(self):
        events, annotations = fixture()
        annotations["trials"][0]["observed"] = "both"
        result = self.report(events, annotations)
        self.assertFalse(result["trials"][0]["success"])
        self.assertEqual(result["latency"]["samples"], 0)

    def test_reused_id_in_different_session_cannot_supply_wrong_host_result(self):
        events, annotations = fixture()
        events[-2]["sessionGeneration"] = 4
        result = self.report(events, annotations)
        self.assertEqual(result["trials"][0]["host"], [])
        self.assertEqual(result["latency"]["samples"], 0)

    def test_multiple_clicks_in_one_trial_are_explicitly_ambiguous(self):
        events, annotations = fixture()
        another = deepcopy(events[1])
        another.update(sequence=3, gestureId=8, at=220)
        events.insert(-1, another)
        result = self.report(events, annotations)
        self.assertTrue(result["trials"][0]["traceCorrelationAmbiguous"])
        self.assertEqual(result["latency"]["samples"], 0)

    def test_identity_mismatch_and_duplicate_run_are_rejected(self):
        for key in ("mode", "runId", "executableSha256"):
            events, annotations = fixture()
            annotations[key] = "different"
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                self.report(events, annotations)
        events, annotations = fixture()
        events.append(events[0])
        with self.assertRaisesRegex(ValueError, "exactly one"):
            self.report(events, annotations)

    def test_non_object_records_and_annotations_fail_explicitly(self):
        events, annotations = fixture()
        for invalid_events, invalid_annotations in (([1], annotations), (events, [])):
            with self.assertRaisesRegex(ValueError, "objects"):
                self.report(invalid_events, invalid_annotations)
        annotations["trials"] = ["mistyped"]
        with self.assertRaisesRegex(ValueError, "must be an object"):
            self.report(events, annotations)

    def test_overlapping_duplicate_or_invalid_intent_trials_are_rejected(self):
        for mutation in ("overlap", "duplicate", "invalid-intent"):
            events, annotations = fixture()
            second = deepcopy(annotations["trials"][0])
            second["trialId"] = "two"
            if mutation == "duplicate":
                second["trialId"] = "one"
            elif mutation == "invalid-intent":
                second["intent"] = "neighbor"
            annotations["trials"].append(second)
            with self.assertRaises(ValueError):
                self.report(events, annotations)

    def test_adjacent_trials_have_half_open_boundaries(self):
        events, annotations = fixture()
        first = annotations["trials"][0]
        first.update(startAt=0, endAt=120)
        second = deepcopy(first)
        second.update(trialId="two", startAt=120, endAt=300)
        annotations["trials"].append(second)
        result = self.report(events, annotations)
        self.assertEqual([row["observedGestureCount"] for row in result["trials"]], [0, 1])

    def test_gaps_remain_visible_and_neighbor_is_not_start_denominator(self):
        events, annotations = fixture()
        events.insert(1, {"kind": "gap", "stream": "input", "after": 0, "before": 8})
        self.assertEqual(self.report(events, annotations)["latency"]["samples"], 0)
        annotations["trials"][0].update(intent="neighbor", expectedAction="neighbor",
                                         observed="neighbor-unchanged")
        result = self.report(events, annotations)
        self.assertTrue(result["trials"][0]["journalHasGaps"])
        self.assertTrue(result["trials"][0]["success"])
        self.assertEqual(result["annotatedStartTrials"], 0)

    def test_hide_and_cancel_have_separate_expected_outcomes(self):
        for action, observed in (("hide", "hidden"), ("cancel", "none")):
            events, annotations = fixture()
            annotations["trials"][0].update(expectedAction=action, observed=observed)
            result = self.report(events, annotations)
            self.assertTrue(result["trials"][0]["success"])
            self.assertEqual(result["latency"]["samples"], 0)

    def test_missing_dispatch_or_inconsistent_clock_cannot_supply_latency(self):
        for mutation in ("dispatch", "clock", "press"):
            events, annotations = fixture()
            if mutation == "dispatch":
                events[2]["nativeDispatchQueued"] = False
            elif mutation == "clock":
                events[-2]["releasedToObservationMs"] = 40
            else:
                events[-2]["pressedAt"] = 110
            self.assertEqual(self.report(events, annotations)["latency"]["samples"], 0)


class CaptureCollectorTests(unittest.TestCase):
    def collect_fixture(self, directory, mutation=None, seconds=1, limit=None):
        root = Path(directory)
        executable = root / "fixture.exe"
        executable.write_bytes(b"fixture only, never executed")
        snapshot = root / "snapshot.json"
        source = {"schemaVersion": 2, "runId": "fixture", "mode": "point", "processId": 1,
                  "executableSha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                  "exportedAt": 1000, "recentInput": [], "recentHost": []}
        if mutation == "size":
            source["recentInput"] = [{"sequence": i, "event": "left-down", "reason": "x" * 400}
                                     for i in range(1, 100)]
        if mutation == "wrong-binary":
            source["executableSha256"] = "wrong"
        snapshot.write_text(json.dumps(source), encoding="utf-8")
        clock = SimpleNamespace(now=1000)

        def sleep(_):
            clock.now += 100
            if mutation == "identity":
                source["runId"] = "replacement"
                snapshot.write_text(json.dumps(source), encoding="utf-8")
            elif mutation == "structure":
                snapshot.write_text("[]", encoding="utf-8")

        arguments = SimpleNamespace(snapshot=snapshot, executable=executable, mode="point",
                                    seconds=seconds, output=root / "events.ndjson")
        replacements = {"native_clock": lambda: clock.now, "time": SimpleNamespace(sleep=sleep)}
        if limit:
            replacements["MAX_BYTES"] = limit
        with patch.dict(API["collect"].__globals__, replacements), redirect_stdout(io.StringIO()):
            code = API["collect"](arguments)
        self.assertEqual(code, 2 if mutation in {"identity", "stale", "structure", "size"} else 0)
        return arguments.output

    def test_identity_staleness_and_size_end_explicitly(self):
        for mutation, expected in (("identity", "source-identity-changed"),
                                   ("structure", "source-invalid"),
                                   ("stale", "source-stale-or-clock-invalid"),
                                   ("size", "journal-size-limit"), (None, "duration")):
            with tempfile.TemporaryDirectory() as directory:
                path = self.collect_fixture(directory, mutation, seconds=3 if mutation == "stale" else 1,
                                            limit=8192 if mutation == "size" else None)
                records = API["read_events"](path)
                self.assertEqual(records[-1]["reason"], expected)
                self.assertEqual(records[0]["kind"], "run")
                self.assertLessEqual(path.stat().st_size, 8192 if mutation == "size" else API["MAX_BYTES"])

    def test_refuses_wrong_binary_before_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "binary hash"):
                self.collect_fixture(directory, "wrong-binary")
            self.assertFalse((Path(directory) / "events.ndjson").exists())

    def test_never_overwrites_previous_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.collect_fixture(directory)
            original = path.read_bytes()
            with self.assertRaises(FileExistsError):
                self.collect_fixture(directory)
            self.assertEqual(path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
