"""Record CI phase duration and exit status without changing command arguments."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import time


def record(phase, started, exit_code):
    destination = os.environ.get('XDSTARTER_CI_EVIDENCE')
    if not destination:
        return
    identity = {key: os.environ.get(key, '') for key in
                ('GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT', 'RUNNER_OS', 'RUNNER_ARCH')}
    if (not re.fullmatch(r'[a-z][a-z0-9-]{0,60}', phase)
            or not re.fullmatch(r'[0-9a-f]{40}', identity['GITHUB_SHA'])
            or not re.fullmatch(r'[1-9][0-9]*', identity['GITHUB_RUN_ID'])
            or not re.fullmatch(r'[1-9][0-9]*', identity['GITHUB_RUN_ATTEMPT'])
            or identity['RUNNER_OS'] not in ('Linux', 'Windows') or identity['RUNNER_ARCH'] != 'X64'):
        raise ValueError('Invalid CI timing identity')
    path = Path(destination) / 'timings.json'
    data = {'identity': identity, 'commands': []}
    if path.exists():
        data = json.loads(path.read_text(encoding='utf-8'))
        if data['identity'] != identity:
            raise ValueError('CI timing identity changed')
    data['commands'].append({'phase': phase, 'elapsed_ms': (time.monotonic_ns() - started) / 1e6,
                             'exit_code': exit_code})
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)
