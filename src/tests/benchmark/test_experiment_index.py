#!/usr/bin/env python3
import json
from pathlib import Path
import tempfile
import unittest

from experiment_index import document_kind, findings, render, report_files


class ExperimentIndexTest(unittest.TestCase):
    def test_timing_adoption_and_design_notes_are_discoverable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ['MEMORY_TIMING.md', 'MEMORY_ADOPTION.md', 'BUDGET_DESIGN.md',
                     'PROFILE.md', 'README.md', 'EXPERIMENT_INDEX.md', 'CONTROLLED_RESULTS.md']
            for name in names:
                (root / name).write_text('# Document\n')
            self.assertEqual({path.name for path in report_files(root)}, set(names[:4]))
            self.assertEqual(document_kind(root / 'BUDGET_DESIGN.md'), 'Design/plan')
            self.assertEqual(document_kind(root / 'MEMORY_ADOPTION.md'), 'Adoption/decision')

    def test_pending_result_does_not_become_a_gain_or_default(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            row = dict(experiment='candidate', game='combat', complete=False,
                       valid=3, expected=8, invalid=2, original_report='CASE_RESULTS.md')
            data = dict(comparisons=[row], valid_observations=3,
                        expected_observations=8, invalid_observations=2)
            (root / 'CONTROLLED_RESULTS.json').write_text(json.dumps(data))
            (root / 'EXPERIMENT_STATUS.json').write_text(json.dumps(dict(policy='Policy', experiments={})))
            report = root / 'CASE_RESULTS.md'
            report.write_text('# Candidate\n\nHistorical throughput improved 20%, but confirmation regressed 2%.\n')
            first = render(root)
            self.assertIn('3/8; 2 invalid', first)
            self.assertIn('Reassessment pending', first)
            self.assertIn('confirmation regressed 2%', first)
            row.update(complete=True, valid=8, cpu_throughput_change_percent=3.0,
                       wall_throughput_change_percent=-1.0,
                       native_instruction_change_percent=-4.0, favorable_pairs=3,
                       paired_cpu_throughput_changes_percent=[1, 2, -1, 4])
            data['valid_observations'] = 8
            (root / 'CONTROLLED_RESULTS.json').write_text(json.dumps(data))
            second = render(root)
            self.assertIn('+3.00% | -1.00% | -4.00%', second)
            self.assertIn('No new default adopted', second)
            report.write_text(report.read_text() + '\nA later repeat found no speedup.\n')
            self.assertIn('A later repeat found no speedup.', render(root))

    def test_historical_metrics_and_all_table_rows_stay_distinct(self):
        adoption = 'Measured 5.95% less worker CPU for A and 1.31% more for B.'
        self.assertEqual(findings('# Adoption\n\n' + adoption)[0][1], adoption)
        source = ('# Measurements\n\nCPU time fell 10%; wall throughput regressed 3%.\n\n'
                  '| Game | CPU time | Throughput |\n| --- | ---: | ---: |\n'
                  '| A | -10% | -3% |\n| B | +2% | +4% |\n')
        parts = findings(source)
        self.assertIn('CPU time fell 10%; wall throughput regressed 3%.', parts[0][1])
        self.assertIn('| 1 | A | -10% | -3% |', parts[1][1])
        self.assertIn('| 2 | B | +2% | +4% |', parts[1][1])
        numbered = findings('# Results\n\n| # | Throughput |\n| --- | --- |\n| 1 | +2% |\n')
        self.assertEqual(numbered[0][1].splitlines()[0], '| # | Throughput |')

    def test_code_examples_are_not_measurements(self):
        self.assertEqual(findings('# Example\n\n```\nthroughput +100%\n\nslower 90%\n```\n'), [])


if __name__ == '__main__':
    unittest.main()
