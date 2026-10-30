#!/usr/bin/env python3
"""Sample a bounded product process on a disposable GitHub-hosted Windows runner."""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
SAMPLE_COUNT = 21
INTERVAL_NS = 1_000_000_000


def require_hosted(environment, system):
    if (system != 'nt' or environment.get('GITHUB_ACTIONS') != 'true' or
            environment.get('RUNNER_ENVIRONMENT') != 'github-hosted' or
            environment.get('RUNNER_OS') != 'Windows'):
        raise ValueError('resident sampling requires a disposable GitHub-hosted Windows runner')


def summarize(samples):
    if not isinstance(samples, list) or len(samples) != SAMPLE_COUNT:
        raise ValueError('missing resident samples')
    intervals = []
    for index, sample in enumerate(samples):
        keys = ('atNs', 'cpu100ns', 'workingSetBytes', 'privateBytes', 'visibleTopLevelWindows', 'observerNs')
        if any(type(sample.get(key)) is not int or sample[key] < 0 for key in keys):
            raise ValueError('invalid resident sample')
        if not sample['workingSetBytes'] or not sample['privateBytes']:
            raise ValueError('empty process memory sample')
        if index:
            previous = samples[index - 1]
            elapsed = sample['atNs'] - previous['atNs']
            cpu = sample['cpu100ns'] - previous['cpu100ns']
            if elapsed <= 0 or cpu < 0:
                raise ValueError('nonmonotonic sample')
            intervals.append({'elapsedNs': elapsed, 'cpuPercentOneCore': cpu * 10000 / elapsed})
    return {'intervals': intervals,
            'allSamplesWithoutVisibleTopLevelWindows':
                all(sample['visibleTopLevelWindows'] == 0 for sample in samples),
            'maxObserverNs': max(sample['observerNs'] for sample in samples)}


class ProcessCounters:
    class Memory(ctypes.Structure):
        _fields_ = [('cb', wintypes.DWORD), ('pageFaultCount', wintypes.DWORD),
                    ('peakWorkingSet', ctypes.c_size_t), ('workingSet', ctypes.c_size_t),
                    ('peakPagedPool', ctypes.c_size_t), ('pagedPool', ctypes.c_size_t),
                    ('peakNonPagedPool', ctypes.c_size_t), ('nonPagedPool', ctypes.c_size_t),
                    ('pagefile', ctypes.c_size_t), ('peakPagefile', ctypes.c_size_t),
                    ('privateUsage', ctypes.c_size_t)]

    def __init__(self, process):
        self.process = process
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        self.kernel.GetProcessTimes.argtypes = [wintypes.HANDLE, *([ctypes.POINTER(wintypes.FILETIME)] * 4)]
        self.kernel.GetProcessTimes.restype = wintypes.BOOL
        self.kernel.K32GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(self.Memory), wintypes.DWORD]
        self.kernel.K32GetProcessMemoryInfo.restype = wintypes.BOOL
        self.callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        self.user.EnumWindows.argtypes = [self.callback_type, wintypes.LPARAM]
        self.user.EnumWindows.restype = wintypes.BOOL
        self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.user.GetWindowThreadProcessId.restype = wintypes.DWORD
        self.user.IsWindowVisible.argtypes = [wintypes.HWND]
        self.user.IsWindowVisible.restype = wintypes.BOOL

    def read(self):
        start = time.perf_counter_ns()
        creation, exit_time, kernel, user = (wintypes.FILETIME() for _ in range(4))
        if not self.kernel.GetProcessTimes(self.process._handle, ctypes.byref(creation), ctypes.byref(exit_time),
                                           ctypes.byref(kernel), ctypes.byref(user)):
            raise ctypes.WinError(ctypes.get_last_error())
        memory = self.Memory()
        memory.cb = ctypes.sizeof(memory)
        if not self.kernel.K32GetProcessMemoryInfo(self.process._handle, ctypes.byref(memory), memory.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        visible = []

        @self.callback_type
        def collect(hwnd, _):
            pid = wintypes.DWORD()
            if (self.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid)) and
                    pid.value == self.process.pid and self.user.IsWindowVisible(hwnd)):
                visible.append(hwnd)
            return True

        if not self.user.EnumWindows(collect, 0):
            raise ctypes.WinError(ctypes.get_last_error())
        end = time.perf_counter_ns()
        ticks = lambda value: (value.dwHighDateTime << 32) | value.dwLowDateTime
        return {'atNs': (start + end) // 2, 'cpu100ns': ticks(kernel) + ticks(user),
                'workingSetBytes': memory.workingSet, 'privateBytes': memory.privateUsage,
                'visibleTopLevelWindows': len(visible), 'observerNs': end - start}


def read_snapshot(path, pid, digest):
    # The writer atomically replaces this file; a short retry handles a reader
    # observing the replacement without accepting partial or foreign records.
    deadline = time.monotonic() + 2
    while True:
        try:
            snapshot = json.loads(path.read_text(encoding='utf-8'))
            if snapshot.get('processId') != pid or snapshot.get('executableSha256') != digest:
                raise ValueError('diagnostic process identity mismatch')
            return snapshot
        except (OSError, json.JSONDecodeError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(.05)


def run(executable, directory):
    require_hosted(os.environ, os.name)
    executable = executable.resolve(strict=True)
    directory = directory.resolve()
    if (executable.name.lower() != 'xdstarter.exe' or not executable.is_relative_to(ROOT / 'build') or
            not directory.is_relative_to(ROOT / 'build')):
        raise ValueError('the executable and a new evidence directory must be under build')
    directory.mkdir(parents=True, exist_ok=False)
    with executable.open('rb') as binary:
        digest = hashlib.file_digest(binary, 'sha256').hexdigest()
    info = subprocess.run([str(executable), '--build-info'], capture_output=True, text=True, timeout=10, check=True)
    build_info = json.loads(info.stdout)
    snapshot_path = directory / 'snapshot.json'
    environment = os.environ.copy()
    environment['QT_FORCE_STDERR_LOGGING'] = '1'
    environment['QT_MESSAGE_PATTERN'] = '%{type}: %{message}'
    with (directory / 'stdout.log').open('wb') as output, (directory / 'stderr.log').open('wb') as errors:
        process = subprocess.Popen([str(executable), '--layout-file', str(directory / 'layout.json'),
                                    '--start-capture-mode', 'point', '--start-capture-diagnostics', str(snapshot_path),
                                    '--start-capture-duration', '40'], env=environment,
                                   stdout=output, stderr=errors, creationflags=subprocess.CREATE_NO_WINDOW)
        samples = []
        try:
            deadline = time.monotonic() + 12
            while not snapshot_path.exists():
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('product did not publish its own diagnostic snapshot')
                time.sleep(.05)
            initial = read_snapshot(snapshot_path, process.pid, digest)
            counters = ProcessCounters(process)
            time.sleep(2)  # Separate initial startup from the sampled interval.
            next_sample = time.perf_counter_ns()
            for _ in range(SAMPLE_COUNT):
                remaining = next_sample - time.perf_counter_ns()
                if remaining > 0:
                    time.sleep(remaining / 1_000_000_000)
                if process.poll() is not None:
                    raise RuntimeError('product exited before sampling completed')
                samples.append(counters.read())
                next_sample += INTERVAL_NS
            observed = read_snapshot(snapshot_path, process.pid, digest)
            exit_code = process.wait(timeout=45)
            final = read_snapshot(snapshot_path, process.pid, digest)
            summary = summarize(samples)
            result = {'schemaVersion': 1, 'processId': process.pid, 'executableSha256': digest,
                      'buildInfo': build_info,
                      'ci': {key: environment.get(key, '') for key in
                             ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT', 'RUNNER_OS', 'RUNNER_ARCH')},
                      'system': {'platform': platform.platform(), 'logicalProcessors': os.cpu_count()},
                      'diagnostic': {'intervalMs': 250, 'initialHookInstalled': initial.get('hookInstalled'),
                                     'initialSessionActive': initial.get('sessionActive'),
                                     'observedHookInstalled': observed.get('hookInstalled'),
                                     'observedSessionActive': observed.get('sessionActive'),
                                     'finalHookInstalled': final.get('hookInstalled'),
                                     'finalSessionActive': final.get('sessionActive'),
                                     'writeFailures': final.get('diagnosticWriteFailures'),
                                     'runId': initial.get('runId')},
                      'samples': samples, 'summary': summary, 'productExit': exit_code,
                      'limitations': ['250ms diagnostic writes and window enumeration affect measurements',
                                      'visibility is sampled and not a continuous physical observation',
                                      'hosted Windows Server is not Windows 11 hardware acceptance']}
            (directory / 'resident-sample.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
            print(json.dumps({'productExit': exit_code, 'samples': len(samples),
                              'allSamplesWithoutVisibleTopLevelWindows':
                                  summary['allSamplesWithoutVisibleTopLevelWindows']}))
            if (exit_code or final.get('hookInstalled') is not False or final.get('sessionActive') is not False or
                    initial.get('runId') != observed.get('runId') or initial.get('runId') != final.get('runId')):
                raise RuntimeError('bounded product run or capture cleanup failed')
        except Exception as error:
            (directory / 'failure.json').write_text(json.dumps({
                'processId': process.pid, 'executableSha256': digest,
                'errorType': type(error).__name__, 'error': str(error), 'samples': samples,
                'ci': {key: environment.get(key, '') for key in
                       ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')}}, indent=2) + '\n',
                encoding='utf-8')
            raise
        finally:
            # The product owns its exit timer. On timeout only this spawned
            # process is terminated, and the failure remains in the artifact.
            if process.poll() is None:
                try:
                    process.wait(timeout=45)
                except subprocess.TimeoutExpired as error:
                    process.kill()
                    process.wait(timeout=5)
                    (directory / 'forced-shutdown.json').write_text(json.dumps({
                        'processId': process.pid, 'reason': 'product-exit-timeout'}) + '\n', encoding='utf-8')
                    raise RuntimeError('isolated product did not exit on its own timer') from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        run(args.executable, args.output)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, RuntimeError) as error:
        print(f'Resident sampling failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
