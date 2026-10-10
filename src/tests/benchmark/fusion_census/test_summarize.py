import json
import unittest
from pathlib import Path
from summarize import summarize


class CensusSummary(unittest.TestCase):
    def test_returns_and_noncall_edges_do_not_inflate_calls(self):
        counts = {'arm_leaf_call': 10, 'arm_leaf_return': 8,
                  'arm_indirect_call': 20, 'arm_indirect_return': 18,
                  'arm_prefix_call': 5, 'thumb_fused_call': 4,
                  'arm_dispatch_call': 3, 'thumb_interpreted_call': 2,
                  'thumb_fused_branch': 1000}
        capture = {'region_entries': 5000, 'region_returns': 5000,
                   'sites': [dict(kind=k, pc=4096, target=8192, count=n)
                             for k, n in counts.items()]}
        result = summarize([capture])
        self.assertEqual(result['call_count'], 44)
        self.assertEqual(result['complete_fused_calls'], 26)
        self.assertEqual(result['partial_fused_calls'], 13)
        self.assertEqual(result['ordinary_dispatch_calls'], 5)
        self.assertEqual(result['early_fused_exits'], 4)
        self.assertAlmostEqual(sum(result['percent'].values()), 100)

    def test_rejects_unpaused_boundary(self):
        with self.assertRaisesRegex(AssertionError, 'completed region boundary'):
            summarize([{'region_entries': 1, 'region_returns': 0, 'sites': []}])

    def test_recorded_pools_recompute_from_all_site_counts(self):
        report = json.loads((Path(__file__).parent.parent / 'FUSION_CENSUS_RESULTS.json').read_text())
        for game, expected in report['pooled'].items():
            captures = [r['census'] for r in report['captures'] if r['game'] == game]
            self.assertEqual(summarize(captures), expected)
        for row in report['captures']:
            self.assertEqual(summarize([row['census']]), row['summary'])


if __name__ == '__main__':
    unittest.main()
