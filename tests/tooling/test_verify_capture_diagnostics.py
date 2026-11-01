"""Bounded diagnostic-read races; no product hooks or native input."""

import ctypes
import json
import os
from pathlib import Path
import runpy
import tempfile
import threading
import unittest
from unittest.mock import Mock


API = runpy.run_path(str(Path(__file__).resolve().parents[2] / "scripts/verify_capture_diagnostics.py"))


class DiagnosticReadTests(unittest.TestCase):
    def test_initial_open_and_recovery_poll_tolerate_only_bounded_access_conflicts(self):
        initial = Mock(side_effect=[FileNotFoundError("not yet published"), PermissionError("busy"), b"opened"])
        opened, conflicts = API["await_snapshot"](initial, lambda _: True, .5, interval=.001)
        self.assertEqual((opened, conflicts), (b"opened", 2))

        old = {"runId": "same", "exportedAt": 100}
        before = {"runId": "same", "processId": 7, "executableSha256": "hash",
                  "exportedAt": 100, "diagnosticWriteFailures": 1}
        no_failures = {**before, "exportedAt": 101, "diagnosticWriteFailures": 0}
        recovered = {**before, "exportedAt": 101}
        reader = Mock(side_effect=[PermissionError("busy"), before, no_failures, recovered])
        found, conflicts = API["await_snapshot"](
            reader, lambda value: API["recovery_observed"](value, old, 7, "hash"), .5, interval=.001)
        self.assertEqual((found, conflicts), (recovered, 1))
        self.assertEqual(reader.call_count, 4)

    def test_persistent_conflict_and_unrecovered_snapshot_fail(self):
        with self.assertRaisesRegex(RuntimeError, "read retries=[1-9]"):
            API["await_snapshot"](Mock(side_effect=PermissionError("denied")), lambda _: True, .02, interval=.001)
        with self.assertRaisesRegex(RuntimeError, "read retries=0"):
            API["await_snapshot"](lambda: {"exportedAt": 100}, lambda _: False, .02, interval=.001)

    def test_corruption_identity_change_and_product_exit_are_not_retried(self):
        reader = Mock(side_effect=json.JSONDecodeError("bad", "x", 0))
        with self.assertRaises(json.JSONDecodeError):
            API["await_snapshot"](reader, lambda _: True, .2, interval=.001)
        self.assertEqual(reader.call_count, 1)
        candidate = {"runId": "changed", "processId": 7, "executableSha256": "hash", "exportedAt": 101}
        with self.assertRaisesRegex(ValueError, "identity mismatch"):
            API["recovery_observed"](candidate, {"runId": "same", "exportedAt": 100}, 7, "hash")
        with self.assertRaisesRegex(RuntimeError, "product exited"):
            API["await_snapshot"](lambda: b"unexpected", lambda _: True, .2,
                                  process=Mock(poll=Mock(return_value=1)), interval=.001)

    @unittest.skipUnless(os.name == "nt", "Windows sharing rules are required")
    def test_real_windows_exclusive_handle_blocks_read_then_recovers(self):
        from ctypes import wintypes

        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                      wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
        kernel.CloseHandle.restype = wintypes.BOOL
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "snapshot.json"
            path.write_bytes(b'{"runId":"same"}')
            handle = kernel.CreateFileW(str(path), 0x80000000, 0, None, 3, 0x80, None)
            if handle == ctypes.c_void_p(-1).value:
                raise ctypes.WinError(ctypes.get_last_error())
            closer = threading.Timer(.1, kernel.CloseHandle, args=(handle,))
            closer.start()
            try:
                data, conflicts = API["await_snapshot"](path.read_bytes, lambda _: True, 2, interval=.01)
                self.assertEqual(json.loads(data), {"runId": "same"})
                self.assertGreater(conflicts, 0)
            finally:
                closer.join()


if __name__ == "__main__":
    unittest.main()
