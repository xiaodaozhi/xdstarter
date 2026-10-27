# scripts/start_capture_acceptance.py

"""Bounded, opt-in capture collection and independently annotated trial reports.

No input is generated. No window, hook, taskbar setting or product process is
controlled. Observed native-menu outcomes must come from a separate observer.
"""

import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import time


MAX_BYTES = 16 * 1024 * 1024
OUTCOMES = {"xdstarter-only", "native-only", "both", "none", "hidden",
            "neighbor-unchanged", "unknown"}


def read_json(path, limit=4 * 1024 * 1024):
    with Path(path).open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError("JSON exceeds the bounded input size")
    return json.loads(data)


def native_clock():
    if os.name != "nt":
        raise ValueError("physical acceptance collection requires Windows GetTickCount64")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetTickCount64.restype = ctypes.c_ulonglong
    return kernel.GetTickCount64()


def read_events(path):
    if Path(path).stat().st_size > MAX_BYTES:
        raise ValueError("journal exceeds the bounded input size")
    events = []
    with Path(path).open(encoding="utf-8") as stream:
        for line in stream:
            if len(events) >= 100000:
                raise ValueError("too many journal records")
            events.append(json.loads(line))
    return events


def collect(args):
    if not 1 <= args.seconds <= 900:
        raise ValueError("collection duration must be 1..900 seconds")
    expected = "point" if args.mode == "point" else "experimental-area-cache"
    snapshot = read_json(args.snapshot)
    if not isinstance(snapshot, dict):
        raise ValueError("diagnostic root must be an object")
    if snapshot.get("schemaVersion") != 2 or not snapshot.get("runId"):
        raise ValueError("schema 2 diagnostic with a process runId is required")
    if snapshot.get("mode") not in {args.mode, expected}:
        raise ValueError("diagnostic mode does not match the requested control group")
    run_id = snapshot["runId"]
    digest = hashlib.sha256(Path(args.executable).read_bytes()).hexdigest()
    if snapshot.get("executableSha256") != digest:
        raise ValueError("running diagnostic binary hash does not match the selected executable")
    began = native_clock()
    # Exclusive creation prevents silently replacing an earlier acceptance run.
    with Path(args.output).open("xb") as output:
        size = 0

        def write(record, final=False):
            nonlocal size
            payload = (json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n").encode()
            if size + len(payload) > MAX_BYTES - (0 if final else 4096):
                return False
            output.write(payload)
            output.flush()
            size += len(payload)
            return True

        header = {"kind": "run", "schemaVersion": 1, "runId": run_id,
                  "mode": args.mode, "executableSha256": digest, "at": began,
                  "processId": snapshot.get("processId"),
                  "clock": "GetTickCount64 milliseconds", "physicalInputGenerated": False,
                  "nativeMenuOutcome": "requires independent per-trial annotation"}
        write(header)
        last = {"input": 0, "host": 0, "window-event": 0}
        errors = 0
        summary_at = 0
        ended = "duration"
        try:
            while native_clock() - began < args.seconds * 1000:
                try:
                    snapshot = read_json(args.snapshot)
                except (OSError, ValueError):
                    errors += 1
                    time.sleep(0.1)
                    continue
                if not isinstance(snapshot, dict):
                    ended = "source-invalid"
                    break
                if snapshot.get("runId") != run_id or snapshot.get("mode") not in {args.mode, expected}:
                    ended = "source-identity-changed"
                    break
                now = native_clock()
                if snapshot.get("executableSha256") != digest:
                    ended = "source-binary-changed"
                    break
                if not isinstance(snapshot.get("exportedAt"), int) or not 0 <= now - snapshot["exportedAt"] <= 2000:
                    ended = "source-stale-or-clock-invalid"
                    break
                full = False
                for kind, key in (("input", "recentInput"), ("host", "recentHost"),
                                  ("window-event", "recentWindowEvents")):
                    if full:
                        break
                    pending = sorted((row for row in snapshot.get(key, [])
                                      if row.get("sequence", 0) > last[kind]),
                                     key=lambda row: row["sequence"])
                    if pending and pending[0]["sequence"] > last[kind] + 1:
                        full = not write({"kind": "gap", "stream": kind,
                                          "after": last[kind], "before": pending[0]["sequence"]})
                    for row in pending:
                        if full or not write({**row, "kind": kind}):
                            full = True
                            break
                        last[kind] = row["sequence"]
                now = native_clock()
                if not full and now - summary_at >= 1000:
                    keys = ("generation", "hookInstalled", "hookLiveness", "sessionActive",
                            "qualificationLeaseAvailable", "eligibleAtCurrentCursor",
                            "queryCount", "queryDurationHistogram", "queryDurationTotalMs",
                            "queryDurationMaxMs", "inputSequence", "inputEntriesEvicted",
                            "hostEntriesEvicted", "nativeDispatchDropped", "lastQuery",
                            "epoch", "snapshotEpoch", "epochSources", "subscriptionGeneration",
                            "subscriptionRootHresult", "subscriptionStructureHresult",
                            "subscriptionPropertiesHresult", "diagnosticWriteFailures",
                            "windowEventSequence", "windowEventInvalidations",
                            "windowEventAccessibilityObjects", "windowEventEntriesEvicted")
                    full = not write({"kind": "snapshot", "at": now,
                                      **{key: snapshot.get(key) for key in keys}}) or full
                    summary_at = now
                if full:
                    ended = "journal-size-limit"
                    break
                time.sleep(0.1)
        except KeyboardInterrupt:
            ended = "operator-stopped"
        write({"kind": "end", "at": native_clock(), "reason": ended,
               "readErrors": errors, "lastSequences": last}, final=True)
    print(json.dumps({"runId": run_id, "mode": args.mode, "startedAt": began,
                      "endedAt": native_clock(), "reason": ended}, ensure_ascii=False))
    return 0 if ended in {"duration", "operator-stopped"} else 2


def percentile(values, percentile_value):
    return sorted(values)[max(0, math.ceil(len(values) * percentile_value) - 1)] if values else None


def make_report(events, annotations):
    if not isinstance(annotations, dict) or any(not isinstance(event, dict) for event in events):
        raise ValueError("journal records and annotations must be objects")
    headers = [event for event in events if event.get("kind") == "run"]
    if len(headers) != 1:
        raise ValueError("a report must describe exactly one process run")
    header = headers[0]
    for key in ("runId", "mode", "executableSha256"):
        if annotations.get(key) != header.get(key):
            raise ValueError("annotation identity mismatch: " + key)
    if annotations.get("schemaVersion") != 1:
        raise ValueError("unsupported annotation schema")
    if annotations.get("inputSource") not in {"physical-mouse", "posted-fixture", "unknown"}:
        raise ValueError("independently declare the input source")
    if annotations.get("outcomeSource") not in {"operator", "independent-observer", "fixture"}:
        raise ValueError("independently declare the outcome source")
    inputs = [event for event in events if event.get("kind") == "input"]
    hosts = [event for event in events if event.get("kind") == "host"]
    window_events = [event for event in events if event.get("kind") == "window-event"]
    trials = annotations.get("trials", [])
    if not isinstance(trials, list) or len(trials) > 10000:
        raise ValueError("trials must be a bounded list")
    seen = set()
    rows = []
    outcomes = {}
    latencies = []
    intervals = []
    for trial in trials:
        if not isinstance(trial, dict):
            raise ValueError("trial annotation must be an object")
        identity = trial.get("trialId")
        start, end = trial.get("startAt"), trial.get("endAt")
        outcome = trial.get("observed", "unknown")
        action, intent = trial.get("expectedAction"), trial.get("intent")
        if (not isinstance(identity, str) or not identity or identity in seen or
                type(start) is not int or type(end) is not int or start < 0 or start >= end or
                outcome not in OUTCOMES or action not in {"show", "hide", "cancel", "neighbor"} or
                intent not in {"start", "neighbor"} or
                (intent == "neighbor") != (action == "neighbor")):
            raise ValueError("invalid/duplicate trial annotation")
        if any(start < previous_end and previous_start < end
               for previous_start, previous_end in intervals):
            raise ValueError("trial intervals must not overlap")
        intervals.append((start, end))
        seen.add(identity)
        matched = [event for event in inputs if event.get("event") == "left-down" and
                   not event.get("injected") and start <= event.get("at", -1) < end and
                   event.get("gestureId", 0)]
        keys = {(event["originSessionGeneration"], event["gestureId"]) for event in matched}
        linked = [event for event in inputs if
                  (event.get("originSessionGeneration"), event.get("gestureId")) in keys]
        linked_host = [event for event in hosts if
                       (event.get("sessionGeneration"), event.get("gestureId")) in keys]
        referenced_windows = {event.get("nativeWindowEventSequence") for event in linked}
        sessions = {session for session, _ in keys}
        window_context = [event for event in window_events if
                          event.get("sessionGeneration") in sessions and
                          (event.get("sequence") in referenced_windows or
                           start <= event.get("at", -1) < end)]
        gaps = [event for event in events if event.get("kind") == "gap"]
        success_outcome = {"show": "xdstarter-only", "hide": "hidden", "cancel": "none",
                           "neighbor": "neighbor-unchanged"}[action]
        success = None if outcome == "unknown" else outcome == success_outcome
        row = {"trialId": identity, "intent": intent, "expectedAction": action,
               "observed": outcome, "success": success,
               "observedGestureCount": len(keys), "traceCorrelationAmbiguous": len(keys) != 1,
               "input": linked, "host": linked_host, "windowEvents": window_context,
               "journalHasGaps": bool(gaps),
               "nativeMenuOutcomeSource": annotations["outcomeSource"]}
        if intent == "start" and outcome != "unknown":
            outcomes[outcome] = outcomes.get(outcome, 0) + 1
        # Only a uniquely correlated show with measured native foreground can
        # supply a latency. A toggle counter or hide result is not a show.
        if len(keys) == 1 and action == "show" and outcome == "xdstarter-only" and not gaps:
            shows = [event for event in linked_host if event.get("event") == "presentation-result"
                     and event.get("requestedAction") == "show" and event.get("nativeMeasured")
                     and event.get("visible") and not event.get("minimized")
                     and event.get("foreground") is True
                     and start <= event.get("at", -1) < end]
            if len(shows) == 1:
                value = shows[0].get("releasedToObservationMs")
                released = shows[0].get("releasedAt", 0)
                releases = [event for event in linked if event.get("event") == "left-up"
                            and event.get("at") == released and event.get("toggle")
                            and event.get("nativeDispatchQueued")]
                if (type(value) is int and value >= 0 and released > 0 and
                        shows[0]["at"] - released == value and len(releases) == 1 and
                        shows[0].get("pressedAt") == matched[0]["at"]):
                    row["releaseToNativeForegroundMs"] = value
                    latencies.append(value)
        rows.append(row)
    return {"schemaVersion": 1, "runId": header["runId"], "mode": header["mode"],
            "executableSha256": header["executableSha256"], "trials": rows,
            "inputSource": annotations["inputSource"],
            "outcomeSource": annotations["outcomeSource"],
            "annotatedStartTrials": sum(outcomes.values()), "observedStartOutcomes": outcomes,
            "unknownTrials": sum(row["observed"] == "unknown" for row in rows),
            "unobservedStartIntentTrials": sum(row["intent"] == "start" and
                                               row["observedGestureCount"] == 0 for row in rows),
            "latency": {"clock": header.get("clock"), "samples": len(latencies),
                        "scope": "release callback to measured native foreground, successful unique shows only",
                        "p50Ms": percentile(latencies, 0.50), "p95Ms": percentile(latencies, 0.95),
                        "p99Ms": percentile(latencies, 0.99), "maxMs": max(latencies, default=None)},
            "collectorEnded": any(event.get("kind") == "end" for event in events),
            "querySummary": next((event for event in reversed(events)
                                   if event.get("kind") == "snapshot"), None),
            "limits": ["global callbacks are not the intended-Start-click denominator",
                       "missing callbacks do not prove why a hook did not observe a click",
                       "absence of an observed native window does not prove no native menu",
                       "bounded journals can contain gaps; annotations remain independent"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    capture = commands.add_parser("collect")
    capture.add_argument("--snapshot", type=Path, required=True)
    capture.add_argument("--output", type=Path, required=True)
    capture.add_argument("--executable", type=Path, required=True)
    capture.add_argument("--mode", choices=("point", "area-cache"), required=True)
    capture.add_argument("--seconds", type=int, default=120)
    report = commands.add_parser("report")
    report.add_argument("--events", type=Path, required=True)
    report.add_argument("--trials", type=Path, required=True)
    report.add_argument("--output", type=Path, required=True)
    commands.add_parser("clock")
    args = parser.parse_args()
    try:
        if args.command == "collect":
            code = collect(args)
            if code:
                parser.exit(code)
        elif args.command == "report":
            result = make_report(read_events(args.events), read_json(args.trials))
            with args.output.open("x", encoding="utf-8", newline="\n") as output:
                json.dump(result, output, ensure_ascii=False, indent=2)
                output.write("\n")
            print(json.dumps({key: result[key] for key in ("annotatedStartTrials", "latency")}))
        else:
            print(native_clock())
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, "Capture acceptance error: " + str(error) + "\n")


if __name__ == "__main__":
    main()
