"""Safety boundary for the resident-mode graphics fault fixture."""
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'verify_d3d11_failure', ROOT / 'tests/graphics/verify_d3d11_failure.py')
GRAPHICS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GRAPHICS)


class GraphicsFailureTests(unittest.TestCase):
    def test_resident_fixture_requires_both_hosted_runner_markers(self):
        for environment in ({}, {'GITHUB_ACTIONS': 'true'},
                            {'RUNNER_ENVIRONMENT': 'github-hosted'},
                            {'GITHUB_ACTIONS': 'true', 'RUNNER_ENVIRONMENT': 'self-hosted'}):
            with self.subTest(environment=environment), self.assertRaisesRegex(ValueError, 'hosted'):
                GRAPHICS.require_hosted_resident(environment)
        GRAPHICS.require_hosted_resident({'GITHUB_ACTIONS': 'true',
                                          'RUNNER_ENVIRONMENT': 'github-hosted'})


if __name__ == '__main__':
    unittest.main()
