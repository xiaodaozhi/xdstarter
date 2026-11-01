#!/usr/bin/env python3
"""Observe an isolated smoke child from process creation through its stage logs."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import queue
import re
import subprocess
import sys
import threading
import time

from windows_package import generated

STAGE = re.compile(rb'^Smoke stage ([a-z0-9-]+) ([0-9]+) ms\r?$')
MAX_OUTPUT = 1024 * 1024


def observe(command, *, timeout, cwd, env, _on_boundary=None):
    """Measure bounded child output; the optional boundary callback supports test handshakes."""
    started = time.monotonic_ns()
    process = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, bufsize=0)
    created = time.monotonic_ns()
    events = queue.Queue(maxsize=128)

    def read_output():
        try:
            while True:
                chunk = os.read(process.stdout.fileno(), 4096)
                if not chunk:
                    break
                events.put((time.monotonic_ns(), chunk))
        finally:
            process.stdout.close()
            events.put((time.monotonic_ns(), None))

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    deadline = started + int(timeout * 1_000_000_000)
    raw, pending, stages = bytearray(), bytearray(), []
    first_byte = None
    process_exit_observed = None
    pipe_eof_observed = None
    eof = False
    output_exceeded = False

    def observe_exit():
        nonlocal process_exit_observed
        code = process.poll()
        if code is not None and process_exit_observed is None:
            process_exit_observed = time.monotonic_ns()
            if _on_boundary:
                _on_boundary('process-exit')
        return code

    def consume(received, chunk):
        nonlocal first_byte, pipe_eof_observed, eof, output_exceeded
        if chunk is None:
            eof = True
            pipe_eof_observed = received
            if _on_boundary:
                _on_boundary('pipe-eof')
            return
        if first_byte is None:
            first_byte = received
        remaining = MAX_OUTPUT - len(raw)
        if len(chunk) > remaining:
            output_exceeded = True
        accepted = chunk[:remaining]
        raw.extend(accepted)
        pending.extend(accepted)
        while b'\n' in pending:
            line, _, rest = pending.partition(b'\n')
            pending[:] = rest
            match = STAGE.fullmatch(line)
            if match:
                stages.append({'name': match.group(1).decode('ascii'), 'childMilliseconds': int(match.group(2)),
                               'observedMilliseconds': (received - started) / 1_000_000})

    while True:
        now = time.monotonic_ns()
        code = observe_exit()
        if output_exceeded or now >= deadline or (eof and code is not None):
            break
        try:
            consume(*events.get(timeout=min(0.05, (deadline - now) / 1_000_000_000)))
        except queue.Empty:
            pass
    while True:
        try:
            consume(*events.get_nowait())
        except queue.Empty:
            break
    timed_out = time.monotonic_ns() >= deadline and not (eof and observe_exit() is not None)
    if (timed_out or output_exceeded) and observe_exit() is None:
        process.kill()
    exit_code = process.wait(timeout=5)
    observe_exit()
    reader.join(timeout=1)
    while True:
        try:
            consume(*events.get_nowait())
        except queue.Empty:
            break
    finished = time.monotonic_ns()
    first_stage = next((stage for stage in stages if stage['name'] == 'before-application'), None)
    if output_exceeded:
        status = 'output-limit'
    elif timed_out:
        status = ('timeout-after-first-stage' if first_stage and
                  first_stage['observedMilliseconds'] <= timeout * 1000 else 'timeout-before-first-stage')
    elif exit_code != 0:
        status = 'child-failed'
    elif not first_stage or not any(stage['name'] == 'event-loop-returned' for stage in stages):
        status = 'incomplete-stages'
    else:
        status = 'passed'
    return {'status': status, 'exitCode': exit_code, 'timeoutSeconds': timeout,
            'createReturnMilliseconds': (created - started) / 1_000_000,
            'firstByteMilliseconds': None if first_byte is None else (first_byte - started) / 1_000_000,
            'firstStageMilliseconds': None if not first_stage else first_stage['observedMilliseconds'],
            'processExitObservedMilliseconds': (process_exit_observed - started) / 1_000_000,
            'pipeEofObservedMilliseconds': None if pipe_eof_observed is None else (pipe_eof_observed - started) / 1_000_000,
            'finishedMilliseconds': (finished - started) / 1_000_000,
            'stages': stages, 'outputTruncated': output_exceeded,
            'readerFinished': not reader.is_alive()}, bytes(raw)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=float, default=15)
    args = parser.parse_args()
    exe = args.executable.resolve()
    if exe.name.casefold() != 'xdstarter.exe' or not exe.is_file():
        parser.error('an existing xdstarter.exe is required')
    if not 0 < args.timeout_seconds <= 60:
        parser.error('timeout must be in (0, 60] seconds')
    try:
        output = generated(args.output_dir)
        if output.exists():
            raise ValueError('output must be a new dedicated build/ directory')
        output.mkdir(parents=True)
        temporary = output / 'tmp'
        temporary.mkdir()
        env = dict(os.environ)
        env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                   QT_FORCE_STDERR_LOGGING='1', TEMP=str(temporary), TMP=str(temporary))
        if env.get('WINDIR'):
            env['QT_QPA_FONTDIR'] = str(Path(env['WINDIR']) / 'Fonts')
        with exe.open('rb') as stream:
            executable_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
        result, raw = observe([str(exe), '--smoke-test'], timeout=args.timeout_seconds,
                              cwd=exe.parent, env=env)
        (output / 'smoke.log').write_bytes(raw)
        result.update(executableSha256=executable_hash, outputSha256=hashlib.sha256(raw).hexdigest(),
                      platform=platform.platform(), scope='independent offscreen smoke process',
                      limitations=['Popen itself is not interrupted if process creation blocks.',
                                   'Pipe receipt includes scheduling and is not the exact emission time.',
                                   'Process exit is observed by polling and may include scheduling delay.',
                                   'A separate smoke run cannot locate an earlier failed process.'])
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(f"Smoke startup observation: {result['status']} ({output})")
        return 0 if result['status'] == 'passed' else 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        if 'output' in locals() and output.is_dir():
            failure = {'status': 'launcher-failed', 'error': str(error),
                       'scope': 'independent offscreen smoke process'}
            (output / 'result.json').write_text(json.dumps(failure, indent=2) + '\n', encoding='utf-8')
        print(f'Smoke startup observation failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
