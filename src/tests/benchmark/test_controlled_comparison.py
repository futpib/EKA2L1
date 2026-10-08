#!/usr/bin/env python3
"""Reject measurement confounders independently of the observed speed."""
import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from controlled_comparison import replay_input, replay_work, validate_clock, wait_for_builds


class BuildReadiness(unittest.TestCase):
    def run_wait(self, activity):
        clock = [0.0]
        def sleep(seconds):
            clock[0] += seconds
        with patch('controlled_comparison.time.monotonic', side_effect=lambda: clock[0]), \
                patch('controlled_comparison.time.sleep', side_effect=sleep), \
                patch('controlled_comparison.find_build_processes', side_effect=lambda: activity(clock[0])), \
                patch('builtins.print'):
            return wait_for_builds()

    def test_quiet_host_keeps_short_start_check(self):
        result = self.run_wait(lambda now: {})
        self.assertEqual(result['waited_seconds'], 1)
        self.assertEqual(result['required_quiet_seconds'], 1)
        self.assertEqual(result['observed_builds'], {})

    def test_finished_build_has_no_cooldown(self):
        result = self.run_wait(lambda now: {'100': 'cargo'} if now < 5 else {})
        self.assertEqual(result['waited_seconds'], 6)
        self.assertEqual(result['quiet_seconds'], 1)
        self.assertEqual(result['required_quiet_seconds'], 1)

    def test_new_build_resets_quiet_interval(self):
        def activity(now):
            if now < 5:
                return {'100': 'cargo'}
            if 6 <= now < 7:
                return {'200': 'rustc'}
            return {}
        result = self.run_wait(activity)
        self.assertEqual(result['waited_seconds'], 8)
        self.assertEqual(result['quiet_seconds'], 1)
        self.assertEqual(result['observed_builds'], {'100': 'cargo', '200': 'rustc'})


class ReplayValidation(unittest.TestCase):
    def test_frozen_input_cannot_silently_use_default_or_changed_file(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            path = repo / 'long.input'
            path.write_bytes(b'long route\n')
            fingerprint = hashlib.sha256(b'long route\n').hexdigest()
            experiment = dict(inputs=dict(standard=dict(path=str(path), sha256=fingerprint)))
            self.assertEqual(replay_input(experiment, 'standard', repo), (path, fingerprint))
            self.assertEqual(replay_input(experiment, 'combat', repo),
                (repo / 'src/tests/benchmark/sky-force-combat.input', None))
            path.write_bytes(b'changed route\n')
            with self.assertRaisesRegex(RuntimeError, 'Input file differs'):
                replay_input(experiment, 'standard', repo)

    def test_browser_route_and_historical_work_are_checked(self):
        expected = dict(first_virtual_us=42000000, last_virtual_us=60000000,
            first_instructions=6445351805, last_instructions=9476989025, presentations=380)
        report = dict(input_sha256='frozen', measurement=expected.copy())
        self.assertEqual(replay_work(report, 'frozen', expected), expected)
        with self.assertRaisesRegex(RuntimeError, 'Browser replay input differs'):
            replay_work(report, 'different', expected)
        report['measurement']['first_instructions'] += 1
        with self.assertRaisesRegex(RuntimeError, 'Guest work differs'):
            replay_work(report, 'frozen', expected)
        self.assertEqual(replay_work(report), report['measurement'])


class ClockValidation(unittest.TestCase):
    def setUp(self):
        self.plan = dict(worker_cpu=7, frequency_khz=3600000, reference_mhz=2304,
            clock_rules=dict(mean_relative_tolerance=.005, interval_relative_tolerance=.01,
                             minimum_reference_interval_seconds=.02))
        hardware = dict(start_ticks=20, **{name: dict(raw=value, running_fraction=1) for name, value in
            [('user_cycles', 3600000000), ('user_reference_cycles', 2304000000), ('user_instructions', 6000000000)]})
        self.report = dict(worker_affinity=dict(pid=10, tid=11, start_ticks=20, key='10:11'),
            thread_deltas=[dict(pid=10, tid=11, runtime_ns=1000000000, hardware=hardware)],
            clock_samples=[dict(affinity=[7], core_throttle_count=10, package_throttle_count=20,
                policy=dict(scaling_governor='performance', scaling_min_freq='3600000', scaling_max_freq='3600000'),
                user_cycles=[i * 900000000, 0, 0], user_reference_cycles=[i * 576000000, 0, 0])
                for i in range(4)])

    def test_stable_clock(self):
        self.assertTrue(validate_clock(self.report, self.plan)['valid'])

    def test_frequency_and_affinity_changes(self):
        for mutation in ('mean', 'interval', 'affinity', 'policy', 'throttle', 'multiplex', 'identity'):
            with self.subTest(mutation=mutation):
                report = copy.deepcopy(self.report)
                hardware = report['thread_deltas'][0]['hardware']
                if mutation == 'mean':
                    hardware['user_cycles']['raw'] = 3300000000
                elif mutation == 'interval':
                    report['clock_samples'][1]['user_cycles'][0] = 850000000
                elif mutation == 'affinity':
                    report['clock_samples'][1]['affinity'] = [7, 15]
                elif mutation == 'policy':
                    report['clock_samples'][1]['policy']['scaling_max_freq'] = '5100000'
                elif mutation == 'throttle':
                    report['clock_samples'][-1]['core_throttle_count'] += 1
                elif mutation == 'multiplex':
                    hardware['user_cycles']['running_fraction'] = .9
                else:
                    hardware['start_ticks'] += 1
                self.assertFalse(validate_clock(report, self.plan)['valid'])

    def test_missing_samples(self):
        self.report['clock_samples'] = []
        self.assertFalse(validate_clock(self.report, self.plan)['valid'])

    def test_isolation_and_sibling_activity(self):
        self.plan['isolated_cpus'] = [7, 15]
        self.plan['require_support_affinity'] = True
        self.plan['clock_rules']['max_sibling_busy_fraction'] = .02
        for index, sample in enumerate(self.report['clock_samples']):
            sample['monitor_affinity'] = [0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14]
            sample['cgroup_cpus'] = {'/sys/fs/cgroup/user.slice/cpuset.cpus.effective': '0-6,8-14',
                                    '/sys/fs/cgroup/ekabench.slice/cpuset.cpus.effective': '0-15'}
            sample['cpu_ticks'] = {'cpu15': [0, 0, 0, 100 * index, 0, 0, 0, 0]}
        self.assertTrue(validate_clock(self.report, self.plan)['valid'])
        for affinity in (None, [7], [15]):
            bad = copy.deepcopy(self.report)
            bad['clock_samples'][-1]['monitor_affinity'] = affinity
            self.assertFalse(validate_clock(bad, self.plan)['valid'])
        bad = copy.deepcopy(self.report)
        bad['clock_samples'][-1]['cpu_ticks']['cpu15'][0] = 10
        self.assertFalse(validate_clock(bad, self.plan)['valid'])
        self.report['clock_samples'][-1]['cgroup_cpus']['/sys/fs/cgroup/user.slice/cpuset.cpus.effective'] = '0-15'
        self.assertFalse(validate_clock(self.report, self.plan)['valid'])

    def test_missing_hardware(self):
        self.report['thread_deltas'][0]['hardware'] = None
        self.assertFalse(validate_clock(self.report, self.plan)['valid'])

    def test_platform_profile(self):
        self.plan['platform_profile'] = 'performance'
        self.assertFalse(validate_clock(self.report, self.plan)['valid'])
        for sample in self.report['clock_samples']:
            sample['platform_profile'] = 'performance'
        self.assertTrue(validate_clock(self.report, self.plan)['valid'])
        self.report['clock_samples'][-1]['platform_profile'] = 'balanced'
        self.assertFalse(validate_clock(self.report, self.plan)['valid'])
        del self.plan['platform_profile']
        self.assertFalse(validate_clock(self.report, self.plan)['valid'])


if __name__ == '__main__':
    unittest.main()
