import contextlib
import io
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import cleanup_cycles as cleanup
from repository_walk import repository_files

REAL_WORKTREES = cleanup.worktrees


class CleanupTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.build = self.root / 'build'
        self.cycles = self.build / 'cycles'
        for name, value in [('ROOT', self.root), ('BUILD', self.build), ('CYCLES', self.cycles)]:
            patcher = mock.patch.object(cleanup, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        patcher = mock.patch.object(cleanup, 'worktrees', return_value=[self.root])
        patcher.start()
        self.addCleanup(patcher.stop)

    def run_cleanup(self, *args):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return cleanup.main(list(args))

    def test_create_preview_and_explicit_remove(self):
        self.assertEqual(self.run_cleanup('--init', 'issue-154'), 0)
        path = self.cycles / 'issue-154'
        (path / 'result.txt').write_text('scratch')
        self.assertEqual(self.run_cleanup('--path', 'issue-154'), 0)
        self.assertTrue(path.exists())
        with self.assertRaises(SystemExit):
            self.run_cleanup('--path', 'issue-154', '--apply')
        self.assertEqual(self.run_cleanup('--path', 'issue-154', '--apply', '--yes'), 0)
        self.assertFalse(path.exists())

    def test_stale_preview_requires_marker_and_never_deletes(self):
        self.run_cleanup('--init', 'old')
        self.run_cleanup('--init', 'fresh')
        old = self.cycles / 'old'
        stamp = time.time() - 40 * 86400
        os.utime(old / cleanup.MARKER, (stamp, stamp))
        os.utime(old, (stamp, stamp))
        (self.cycles / 'unmarked').mkdir()
        with contextlib.redirect_stdout(io.StringIO()) as output, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(cleanup.main(['--stale-days', '30']), 0)
        self.assertIn('candidate: build', output.getvalue())
        self.assertIn('old', output.getvalue())
        self.assertNotIn('fresh', output.getvalue())
        self.assertTrue(old.exists())
        with self.assertRaises(SystemExit):
            self.run_cleanup('--stale-days', '30', '--apply', '--yes')

    def test_unmarked_and_traversal_are_rejected(self):
        self.cycles.mkdir(parents=True)
        (self.cycles / 'unmarked').mkdir()
        self.assertEqual(self.run_cleanup('--path', 'unmarked', '--apply', '--yes'), 1)
        self.assertEqual(self.run_cleanup('--path', '../tooling', '--apply', '--yes'), 1)
        self.assertTrue((self.cycles / 'unmarked').exists())

    def test_worktree_overlap_and_registry_failure_refuse_removal(self):
        self.run_cleanup('--init', 'owned')
        path = self.cycles / 'owned'
        for registered in (path, path / 'nested', self.cycles):
            with self.subTest(registered=registered), mock.patch.object(cleanup, 'worktrees', return_value=[self.root, registered]):
                self.assertEqual(self.run_cleanup('--path', 'owned', '--apply', '--yes'), 1)
        with mock.patch.object(cleanup, 'worktrees', side_effect=OSError('unavailable')):
            self.assertEqual(self.run_cleanup('--path', 'owned', '--apply', '--yes'), 1)
        self.assertTrue(path.exists())

    def test_legacy_requires_explicit_direct_nonprotected_entry(self):
        self.build.mkdir()
        (self.build / 'tooling').mkdir()
        (self.build / 'old-output').mkdir()
        self.assertEqual(self.run_cleanup('--legacy-path', 'tooling', '--apply', '--yes'), 1)
        self.assertEqual(self.run_cleanup('--legacy-path', '../other', '--apply', '--yes'), 1)
        self.assertEqual(self.run_cleanup('--legacy-path', 'tooling.', '--apply', '--yes'), 1)
        self.assertEqual(self.run_cleanup('--legacy-path', 'old-output'), 0)
        self.assertTrue((self.build / 'old-output').exists())
        self.assertEqual(self.run_cleanup('--legacy-path', 'old-output', '--apply', '--yes'), 0)
        self.assertTrue((self.build / 'tooling').exists())
        self.assertFalse((self.build / 'old-output').exists())

    def test_nested_git_metadata_is_protected(self):
        self.run_cleanup('--init', 'owned')
        path = self.cycles / 'owned'
        (path / '.git').write_text('gitdir: elsewhere')
        self.assertEqual(self.run_cleanup('--path', 'owned', '--apply', '--yes'), 1)
        self.assertTrue(path.exists())

    def test_windows_reparse_attribute_is_detected(self):
        fake = mock.Mock(st_mode=0o100600, st_file_attributes=0x400)
        with mock.patch.object(Path, 'lstat', return_value=fake):
            self.assertTrue(cleanup.linked(Path('fixture')))

    def test_worktree_registry_decodes_nul_delimited_paths(self):
        payload = b'worktree /repo\0HEAD abc\0\0worktree /repo/build/cycles/active\0HEAD def\0\0'
        with mock.patch('cleanup_cycles.subprocess.run', return_value=mock.Mock(stdout=payload)):
            paths = REAL_WORKTREES()
        self.assertEqual(len(paths), 2)
        self.assertEqual(paths[1].name, 'active')

    def test_link_inside_cycle_refuses_deletion(self):
        self.run_cleanup('--init', 'owned')
        path = self.cycles / 'owned'
        outside = self.root / 'keep'
        outside.mkdir()
        (outside / 'value').write_text('keep')
        try:
            (path / 'link').symlink_to(outside, target_is_directory=True)
        except OSError:
            self.skipTest('symlink creation unavailable')
        self.assertEqual(self.run_cleanup('--path', 'owned', '--apply', '--yes'), 1)
        self.assertTrue((outside / 'value').exists())


class RepositoryWalkTests(unittest.TestCase):
    def test_excluded_directories_are_pruned_at_every_level(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('docs/good.md', 'build/deep/secret.md', 'docs/build/deep/ignored.md', '.git/config'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('data')
            seen = []
            real_walk = os.walk
            def walk(*args, **kwargs):
                for directory, subdirs, files in real_walk(*args, **kwargs):
                    seen.append(Path(directory).relative_to(root).as_posix())
                    yield directory, subdirs, files
            with mock.patch('repository_walk.os.walk', side_effect=walk):
                files = repository_files(root, {'build', '.git'})
            self.assertEqual([p.relative_to(root).as_posix() for p in files], ['docs/good.md'])
            self.assertTrue(all('build' not in p and '.git' not in p for p in seen))


if __name__ == '__main__':
    unittest.main()
