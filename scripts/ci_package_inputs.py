"""Plan and verify the public, hash-locked inputs used by Windows packaging."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
ALLOWED_HOSTS = {'download.visualstudio.microsoft.com', 'visualstudio.microsoft.com',
                 'github.com', 'raw.githubusercontent.com', 'download.qt.io'}
NAME = re.compile(r'[A-Za-z0-9_.-]+')


def inputs(root=ROOT):
    config = root / 'packaging/windows'
    runtime, installer, licenses, sources = [json.loads((config / name).read_text(encoding='utf-8'))
        for name in ('runtime.json', 'installer-toolchain.json', 'qt-license-sources.json', 'sources.json')]
    version = runtime['version']
    if not NAME.fullmatch(version):
        raise ValueError('Invalid runtime version')
    inno = installer['inno_setup']
    rows = [(f'crt/{version}/vc_redist.x64.exe', runtime), ('wix.zip', runtime['wix']),
            (f'crt/{version}/Microsoft-VC-runtime-license.docx', runtime['license']),
            ('compiler-setup.exe', inno)]
    rows += [(entry['file'], entry) for entry in inno['materials']]
    rows += [('licenses/' + entry['id'] + '.txt', entry) for entry in licenses['licenses']]
    rows += [('sources/' + entry['file'], entry) for entry in sources['archives']]
    result = []
    for relative, record in rows:
        path = PurePosixPath(relative)
        url = urlsplit(record['url'])
        if (not path.parts or any(not NAME.fullmatch(part) or part in ('.', '..') for part in path.parts)
                or url.scheme != 'https' or url.hostname not in ALLOWED_HOSTS
                or url.username or url.password or url.port not in (None, 443)
                or not re.fullmatch(r'[0-9a-f]{64}', record['sha256'])):
            raise ValueError('Unsafe or unpinned supplier input')
        result.append({'path': 'build/package-tools/' + path.as_posix(),
                       'url': record['url'], 'sha256': record['sha256']})
    if len({row['path'].casefold() for row in result}) != len(result):
        raise ValueError('Duplicate supplier input')
    return sorted(result, key=lambda row: row['path'])


def key(rows):
    data = json.dumps(rows, sort_keys=True, separators=(',', ':')).encode('utf-8')
    return 'xdstarter-windows-inputs-v1-' + hashlib.sha256(data).hexdigest()


def linked(path):
    try:
        info = path.lstat()
    except FileNotFoundError:
        return False
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, 'st_file_attributes', 0) & 0x400)


def verify(rows, root=ROOT, restored=False):
    if restored:
        cache = root / 'build/package-tools'
        allowed = {row['path'].casefold() for row in rows}
        for entry in cache.rglob('*'):
            if linked(entry):
                raise ValueError('Linked supplier cache entry')
            if entry.is_file() and entry.relative_to(root).as_posix().casefold() not in allowed:
                raise ValueError('Unexpected restored supplier input')
    for row in rows:
        path = root / row['path']
        current = path
        while current != root:
            if linked(current):
                raise ValueError('Linked supplier cache path')
            current = current.parent
        if not path.is_file():
            raise ValueError('Missing supplier input: ' + path.name)
        with path.open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != row['sha256']:
            raise ValueError('Supplier input hash mismatch: ' + path.name)


def output(name, value):
    if os.environ.get('GITHUB_OUTPUT'):
        with Path(os.environ['GITHUB_OUTPUT']).open('a', encoding='utf-8') as stream:
            stream.write(f'{name}<<XDSTARTER_OUTPUT\n{value}\nXDSTARTER_OUTPUT\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('plan', 'verify'))
    parser.add_argument('--matched-key', default='')
    parser.add_argument('--restored', action='store_true')
    args = parser.parse_args()
    rows = inputs()
    expected = key(rows)
    if args.mode == 'plan':
        output('key', expected)
        output('paths', '\n'.join(row['path'] for row in rows))
        print(f'Planned {len(rows)} fixed supplier inputs; key={expected}')
    else:
        if args.matched_key and args.matched_key != expected:
            raise ValueError('Partial or unrelated cache key')
        verify(rows, restored=args.restored)
        print(f'Verified sources and SHA-256 for {len(rows)} supplier inputs')


if __name__ == '__main__':
    main()
