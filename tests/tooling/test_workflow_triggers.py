import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class WorkflowTriggerTests(unittest.TestCase):
    def test_daily_and_manual_workflows_have_separate_triggers(self):
        daily = json.loads((ROOT / '.github/workflows/repository-checks.yml').read_text(encoding='utf-8'))
        full = json.loads((ROOT / '.github/workflows/application-build.yml').read_text(encoding='utf-8'))
        self.assertEqual(set(daily['on']), {'push', 'pull_request', 'workflow_dispatch'})
        self.assertEqual(daily['on']['push']['branches'], ['main'])
        self.assertEqual(set(full['on']), {'workflow_dispatch'})
        self.assertTrue({'core', 'windows', 'plan', 'shadow-gate'} <= set(full['jobs']))
        self.assertEqual(set(full['jobs']['shadow-gate']['needs']), {'core', 'windows', 'plan'})


if __name__ == '__main__':
    unittest.main()
