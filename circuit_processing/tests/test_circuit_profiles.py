import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import legacy_circuit_profiles as circuits


class CircuitProfileTests(unittest.TestCase):
    def profile(self):
        return {
            'schema': 1, 'name': 'custom_difference', 'application': 'graph',
            'baseline': [{'node': 0, 'unit': '5QC'}],
            'graph': {
                'border': 'replicate',
                'inputs': {'p': {'dx': 0, 'dy': 0}, 'q': {'dx': 1, 'dy': 0}},
                'operations': [{'id': '0', 'op': 'sub', 'args': ['p', 'q']},
                               {'id': 'magnitude', 'op': 'abs', 'args': ['0']}],
                'output': 'magnitude',
            },
        }

    def test_profile_preserves_user_assignment(self):
        profile = self.profile()
        self.assertEqual(circuits.validate_profile(profile), profile)

    def test_rejects_missing_duplicate_unknown_units_and_forward_refs(self):
        for baseline in ([], [{'node': 0, 'unit': 'unknown'}],
                         [{'node': 0, 'unit': '5RP'}, {'node': 0, 'unit': '5QC'}]):
            profile = self.profile()
            profile['baseline'] = baseline
            with self.assertRaises(ValueError):
                circuits.validate_profile(profile)
        profile = self.profile()
        profile['graph']['operations'][0]['args'][0] = 'magnitude'
        with self.assertRaises(ValueError):
            circuits.validate_profile(profile)

    def test_propagates_per_node_error_and_shift_rounding(self):
        graph = self.profile()['graph']
        self.assertEqual(circuits.error_bound(graph, {'0': 1}), 1)
        self.assertEqual(circuits.error_bound(graph, {'0': 4}), 4)
        graph['operations'].append({'id': 'half', 'op': 'shift', 'bits': 1, 'args': ['magnitude']})
        graph['output'] = 'half'
        self.assertEqual(circuits.error_bound(graph, {'0': 1}), 1)
        self.assertEqual(circuits.error_bound(graph, {'0': 4}), 2)

    def test_detects_overflow_before_approximate_adder(self):
        graph = self.profile()['graph']
        graph['inputs']['p']['scale'] = 100
        self.assertIsNone(circuits.error_bound(graph, {'0': 1}))

    def test_disconnected_high_error_node_does_not_change_output_bound(self):
        graph = self.profile()['graph']
        graph['operations'].append({'id': '1', 'op': 'add', 'args': ['p', 'q']})
        self.assertEqual(circuits.error_bound(graph, {'0': 1, '1': 86}), 1)


if __name__ == '__main__':
    unittest.main()
