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


def verify(executable, directory):
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
            "--start-capture-mode", "point", "--start-capture-diagnostics", str(snapshot),
            "--start-capture-duration", "10"], env=environment, stdout=output, stderr=errors,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            deadline = time.monotonic() + 6
            while not snapshot.exists() and time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError("product exited before diagnostics; exit any existing instance first")
                time.sleep(.05)
            with snapshot.open("rb") as held:
                old = json.load(held)
                if old["processId"] != process.pid or old["executableSha256"] != digest:
                    raise ValueError("diagnostic process/binary identity mismatch")
                # This is an intentional deny-delete reader. Qt 6.8.2's Windows
                # replacement must fail while held and recover after it closes.
                time.sleep(1)
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                recovered = api["read_json"](snapshot)
                if (recovered["exportedAt"] > old["exportedAt"] and
                        recovered.get("diagnosticWriteFailures", 0) >= 1):
                    break
                time.sleep(.05)
            else:
                raise RuntimeError("diagnostic writer did not recover after reader release")
            collection = subprocess.run([sys.executable, str(root / "scripts/start_capture_acceptance.py"),
                "collect", "--snapshot", str(snapshot), "--output", str(directory / "events.ndjson"),
                "--executable", str(executable), "--mode", "point", "--seconds", "2"],
                capture_output=True, text=True, timeout=5)
            exit_code = process.wait(timeout=15)
            final = api["read_json"](snapshot)
            result = {"productExit": exit_code, "collectorExit": collection.returncode,
                "recoveredWriteFailures": recovered["diagnosticWriteFailures"],
                "exportResumed": recovered["exportedAt"] > old["exportedAt"],
                "finalHookInstalled": final["hookInstalled"],
                "finalSessionActive": final["sessionActive"], "sha256": digest,
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
    args = parser.parse_args()
    verify(args.executable, args.output)
