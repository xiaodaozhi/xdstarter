"""Manage explicitly registered development-cycle scratch directories under build/."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
CYCLES = BUILD / 'cycles'
MARKER = '.xdstarter-cycle'
MAGIC = 'xdstarter scratch cycle v1\n'
PROTECTED = {'qt', 'tooling', 'qt-install', 'package', 'package-tools',
             'validation', 'worktrees', 'app-release', 'app-debug', 'core-release',
             'core-debug', 'tmp', 'ci-evidence', 'ci-gate'}
NAME = re.compile(r'[a-z0-9][a-z0-9_-]{0,79}\Z')


def linked(path):
    info = path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(
        getattr(info, 'st_file_attributes', 0) & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0x400))


def check_parents():
    for path in (BUILD, CYCLES):
        if path.exists() or path.is_symlink():
            if linked(path) or not path.is_dir():
                raise ValueError(f'not a real directory: {path}')
    if CYCLES.exists() and not CYCLES.resolve().is_relative_to(BUILD.resolve()):
        raise ValueError('cycles directory escapes build/')


def worktrees():
    result = subprocess.run(['git', 'worktree', 'list', '--porcelain', '-z'], cwd=ROOT,
                            check=True, stdout=subprocess.PIPE)
    paths = []
    for field in result.stdout.split(b'\0'):
        if field.startswith(b'worktree '):
            paths.append(Path(os.fsdecode(field[9:])).resolve())
    if not paths:
        raise ValueError('cannot verify registered worktrees')
    return paths


def overlaps_worktree(path, paths):
    resolved = path.resolve()
    repo = ROOT.resolve()
    for item in paths:
        if item == repo:
            continue
        if resolved == item or resolved.is_relative_to(item) or item.is_relative_to(resolved):
            return True
    return False


def target(name, paths):
    if not NAME.fullmatch(name):
        raise ValueError('cycle name must use lowercase ASCII letters, digits, - or _')
    check_parents()
    path = CYCLES / name
    if linked(path) or not path.is_dir() or not path.resolve().is_relative_to(CYCLES.resolve()):
        raise ValueError(f'not a safe cycle directory: {name}')
    if overlaps_worktree(path, paths):
        raise ValueError(f'cycle overlaps a registered worktree: {name}')
    marker = path / MARKER
    if linked(marker) or not marker.is_file() or marker.read_text(encoding='utf-8') != MAGIC:
        raise ValueError(f'missing or invalid cycle marker: {name}')
    return path


def legacy_target(name, paths):
    if (not name or name in ('.', '..') or name.rstrip(' .') != name or
            Path(name).name != name or any(c in name for c in '/\\:')):
        raise ValueError('legacy path must be one immediate build/ entry')
    if name.casefold() in PROTECTED or name.casefold() == 'cycles':
        raise ValueError('refusing protected build entry')
    check_parents()
    path = BUILD / name
    if (linked(path) or not (path.is_file() or path.is_dir()) or
            not path.resolve().is_relative_to(BUILD.resolve())):
        raise ValueError('legacy entry is linked, unsupported or outside build/')
    resolved = path.resolve()
    for protected in PROTECTED | {'cycles'}:
        protected_path = (BUILD / protected).resolve()
        if resolved == protected_path or resolved.is_relative_to(protected_path) or protected_path.is_relative_to(resolved):
            raise ValueError('legacy entry overlaps a protected directory')
    if overlaps_worktree(path, paths):
        raise ValueError('legacy entry overlaps a registered worktree')
    return path


def newest(path):
    latest = path.stat().st_mtime
    if not path.is_dir():
        return latest
    def failed(error):
        raise error
    for directory, dirs, files in os.walk(path, followlinks=False, onerror=failed):
        for name in dirs + files:
            item = Path(directory) / name
            if name.casefold() == '.git':
                raise ValueError(f'cycle contains Git metadata: {item}')
            if linked(item):
                raise ValueError(f'cycle contains a link or reparse point: {item}')
            latest = max(latest, item.stat().st_mtime)
    return latest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--init', metavar='NAME', help='create a new marked cycle directory')
    action.add_argument('--path', metavar='NAME', help='inspect one marked cycle')
    action.add_argument('--legacy-path', metavar='NAME', help='inspect an identified immediate build/ entry')
    action.add_argument('--stale-days', type=float, help='preview marked cycles older than N days')
    parser.add_argument('--apply', action='store_true', help='delete the explicitly named cycle')
    parser.add_argument('--yes', action='store_true', help='confirm explicit deletion')
    args = parser.parse_args(argv)
    if args.apply and (not (args.path or args.legacy_path) or not args.yes):
        parser.error('--apply requires an explicit path and --yes; stale scans are preview only')
    if args.yes and not args.apply:
        parser.error('--yes requires --apply')
    if args.stale_days is not None and (not 0 <= args.stale_days < float('inf')):
        parser.error('--stale-days must be finite and non-negative')
    try:
        check_parents()
        paths = worktrees()
        if args.init:
            if not NAME.fullmatch(args.init):
                raise ValueError('invalid cycle name')
            CYCLES.mkdir(parents=True, exist_ok=True)
            check_parents()
            path = CYCLES / args.init
            if overlaps_worktree(path, paths):
                raise ValueError('cycle overlaps a registered worktree')
            path.mkdir()
            (path / MARKER).write_text(MAGIC, encoding='utf-8', newline='\n')
            print(f'created: {path.relative_to(ROOT)}')
        elif args.legacy_path:
            path = legacy_target(args.legacy_path, paths)
            newest(path)
            print(f'{"remove" if args.apply else "candidate"}: {path.relative_to(ROOT)}')
            if args.apply:
                legacy_target(args.legacy_path, worktrees())
                newest(path)
                if path.is_dir():
                    shutil.rmtree(path)
                else:
                    path.unlink()
        elif args.path:
            path = target(args.path, paths)
            newest(path)
            print(f'{"remove" if args.apply else "candidate"}: {path.relative_to(ROOT)}')
            if args.apply:
                # Recheck the worktree registry and tree immediately before removal.
                target(args.path, worktrees())
                newest(path)
                shutil.rmtree(path)
        else:
            if not CYCLES.exists():
                print('No registered cycles.')
                return 0
            found = 0
            for path in sorted(CYCLES.iterdir()):
                try:
                    item = target(path.name, paths)
                    age = max(0.0, (time.time() - newest(item)) / 86400)
                    if age >= args.stale_days:
                        print(f'candidate: {item.relative_to(ROOT)} ({age:.1f} days)')
                        found += 1
                except (OSError, ValueError, UnicodeError) as error:
                    print(f'skipped {path.name}: {error}', file=sys.stderr)
            if not found:
                print('No eligible cycles.')
            print('Preview only; inspect and remove individual cycles with --path NAME --apply --yes.')
        return 0
    except (OSError, ValueError, UnicodeError, subprocess.SubprocessError) as error:
        print(f'cleanup refused: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
