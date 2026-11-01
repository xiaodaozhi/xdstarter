# scripts/verify_capture_diagnostics.py

"""Windows diagnostic I/O regression using one bounded, isolated product run."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import time


def await_snapshot(read, accept, timeout, *, process=None, interval=.05):
    """Wait for a readable and acceptable snapshot, without hiding malformed data."""
    deadline = time.monotonic() + timeout
    retries = 0
    last_error = None
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            raise RuntimeError("product exited while awaiting diagnostics")
        try:
            value = read()
        except (FileNotFoundError, PermissionError) as error:
            retries += 1
            last_error = error
        else:
            if accept(value):
                return value, retries
        time.sleep(interval)
    raise RuntimeError(
        f"diagnostic snapshot did not become available and acceptable within {timeout}s; "
        f"read retries={retries}, last read error={last_error!r}"
    )


def recovery_observed(candidate, old, pid, digest):
    if (candidate["processId"] != pid or candidate["executableSha256"] != digest or
            candidate["runId"] != old["runId"]):
        raise ValueError("diagnostic process/binary/run identity mismatch")
    return (candidate["exportedAt"] > old["exportedAt"] and
            candidate.get("diagnosticWriteFailures", 0) >= 1)


def verify(executable, directory, mode="point"):
    root = Path(__file__).resolve().parents[1]
    executable = executable.resolve(strict=True)
    directory = directory.resolve()
    if (os.name != "nt" or executable.name.lower() != "xdstarter.exe" or
            not executable.is_relative_to(root / "build") or
            not directory.is_relative_to(root / "build")):
        raise ValueError("Windows executable and a new evidence directory under build are required")
    directory.mkdir(parents=True, exist_ok=False)
    api = runpy.run_path(str(root / "scripts/start_capture_acceptance.py"))
    snapshot = directory / "snapshot.json"
    digest = hashlib.sha256(executable.read_bytes()).hexdigest()
    environment = os.environ.copy()
    environment["QT_FORCE_STDERR_LOGGING"] = "1"
    with (directory / "stdout.log").open("wb") as output, (directory / "stderr.log").open("wb") as errors:
        process = subprocess.Popen([str(executable), "--layout-file", str(directory / "layout.json"),
            "--start-capture-mode", mode, "--start-capture-diagnostics", str(snapshot),
            "--start-capture-duration", "10"], env=environment, stdout=output, stderr=errors,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            held, initial_retries = await_snapshot(
                lambda: snapshot.open("rb"), lambda _: True, 6, process=process)
            with held:
                old = json.load(held)
                if old["processId"] != process.pid or old["executableSha256"] != digest:
                    raise ValueError("diagnostic process/binary identity mismatch")
                # This is an intentional deny-delete reader. Qt 6.8.2's Windows
                # replacement must fail while held and recover after it closes.
                time.sleep(1)
            recovered, recovery_retries = await_snapshot(
                lambda: api["read_json"](snapshot),
                lambda candidate: recovery_observed(candidate, old, process.pid, digest), 2, process=process)
            collection = subprocess.run([sys.executable, str(root / "scripts/start_capture_acceptance.py"),
                "collect", "--snapshot", str(snapshot), "--output", str(directory / "events.ndjson"),
                "--executable", str(executable), "--mode", mode, "--seconds", "2"],
                capture_output=True, text=True, timeout=5)
            exit_code = process.wait(timeout=15)
            final = api["read_json"](snapshot)
            result = {"mode": mode, "productExit": exit_code, "collectorExit": collection.returncode,
                "recoveredWriteFailures": recovered["diagnosticWriteFailures"],
                "exportResumed": recovered["exportedAt"] > old["exportedAt"],
                "initialReadRetries": initial_retries, "recoveryReadRetries": recovery_retries,
                "finalHookInstalled": final["hookInstalled"],
                "finalSessionActive": final["sessionActive"], "sha256": digest,
                "windowEventSequence": final["windowEventSequence"],
                "exportedWindowEvents": len(final["recentWindowEvents"]),
                "physicalTrials": 0, "collection": collection.stdout.strip(),
                "collectionError": collection.stderr.strip()}
            (directory / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
            print(json.dumps(result))
            if exit_code or collection.returncode or final["hookInstalled"] or final["sessionActive"]:
                raise RuntimeError("diagnostic collection or bounded shutdown failed")
        finally:
            # The product owns its ten-second normal-exit timer. Never kill a
            # pre-existing instance, and don't leave this test's timer unobserved.
            if process.poll() is None:
                process.wait(timeout=15)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--mode", choices=("point", "area-cache"), default="point")
    args = parser.parse_args()
    verify(args.executable, args.output, args.mode)
