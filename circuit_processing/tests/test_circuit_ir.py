"""Synthetic scalar circuits only; no user images or C programs execute here."""
import inspect
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
import circuit_ir as ir

BOUNDS = {'5RP': {'min_error': -3, 'max_error': 4, 'wce': 4},
          '5QC': {'min_error': -1, 'max_error': 1, 'wce': 1}}


def op(name, kind, args, **kw):
    return dict(id=name, op=kind, args=args, integer=True, width=32, signed=True, **kw)


def profile(operations, output=None, baseline=None):
    return dict(schema=3, name='synthetic', application='ir', baseline=baseline or [],
                ir=dict(schema=1, tile=[1, 1], border='replicate', interior_only=False,
                        inputs=[dict(id='x', dx=0, dy=0)], operations=operations,
                        outputs=[dict(value=output or operations[-1]['id'], dx=0, dy=0)]))


class CircuitIRTests(unittest.TestCase):
    def test_local_bounds_and_precise_replacement(self):
        p = profile([op('a', 'add', ['x', 1], unit_node=0)], baseline=[dict(node=0, unit='5RP')])
        self.assertIs(ir.validate_profile(p), p)
        result = ir.analyze(p, [], BOUNDS)
        self.assertEqual(result['error_bound'], 4)
        self.assertEqual(result['site_ranges']['a'], [-2, 260])
        self.assertEqual(ir.analyze(p, [0], BOUNDS)['error_bound'], 0)
        self.assertEqual(ir.analyze(p, [0], BOUNDS, ('a', 4))['error_bound'], 16)

    def test_fanout_accumulates_error(self):
        p = profile([op('a', 'add', ['x', 0], unit_node=0), op('b', 'add', ['a', 'a'])],
                    baseline=[dict(node=0, unit='5RP')])
        self.assertEqual(ir.analyze(p, [], BOUNDS)['error_bound'], 8)
        self.assertEqual(ir.analyze(p, [0], BOUNDS, ('a', 4))['error_bound'], 32)

    def test_negative_shift_and_round_away_cross_zero(self):
        p = profile([op('a', 'sub', [0, 1]), op('s', 'shift', ['a'], bits=1)])
        self.assertEqual(ir.analyze(p, [], BOUNDS)['site_ranges']['s'], [-1, -1])
        p = profile([op('a', 'sub', [0, 1]), op('s', 'round_shift', ['a'], bits=1)])
        self.assertEqual(ir.analyze(p, [], BOUNDS)['site_ranges']['s'], [-1, -1])
        p = profile([op('a', 'sub', [0, 1]), op('s', 'round_shift', ['a'], bits=4)])
        bound = ir.analyze(p, [], BOUNDS, ('a', 4))['error_bound']
        self.assertEqual(bound, 2)
        self.assertGreaterEqual(bound, abs(ir._round_away(-8/16)-ir._round_away(8/16)))

    def test_ties_even_and_truncation(self):
        p = profile([op('r', 'round_even', [-2.5])])
        self.assertEqual(ir.analyze(p, [], {})['site_ranges']['r'], [-2, -2])
        p = profile([op('r', 'trunc', [-2.9])])
        self.assertEqual(ir.analyze(p, [], {})['site_ranges']['r'], [-2, -2])

    def test_shared_groups_and_mixed_units(self):
        p = profile([op('a', 'add', ['x', 0], unit_node=0),
                     op('b', 'add', ['a', 0], unit_node=0),
                     op('c', 'sub', ['b', 0], unit_node=1)],
                    baseline=[dict(node=0, unit='5RP'), dict(node=1, unit='5QC')])
        ir.validate_profile(p)
        self.assertEqual(ir.analyze(p, [], BOUNDS)['error_bound'], 9)
        self.assertEqual(ir.analyze(p, [0], BOUNDS)['error_bound'], 1)
        self.assertEqual(ir.analyze(p, [1], BOUNDS)['error_bound'], 8)

    def test_exact_overflow_rejected_and_signed64_supported(self):
        p = profile([op('a', 'scale', ['x'], factor=2**24)])
        with self.assertRaisesRegex(ValueError, 'overflow'):
            ir.validate_profile(p)
        p['ir']['operations'][0]['width'] = 64
        ir.validate_profile(p)
        self.assertEqual(ir.analyze(p, [], {})['error_bound'], 0)
        p['ir']['operations'][0]['factor'] = 2**40
        with self.assertRaises(ValueError):
            ir.validate_profile(p)

    def test_approximate_range_failure_is_explicit(self):
        p = profile([op('s', 'scale', ['x'], factor=17),
                     op('a', 'sub', ['s', 0], unit_node=0)],
                    baseline=[dict(node=0, unit='5RP')])
        ir.validate_profile(p)
        self.assertIn('12-bit', ir.analyze(p, [], BOUNDS)['invalid_reason'])
        self.assertIsNone(ir.analyze(p, [0], BOUNDS)['invalid_reason'])
        self.assertIn('13-bit', ir.analyze(p, [0], BOUNDS, ('s', 4))['invalid_reason'])

    def test_float_roundoff_nonzero_only(self):
        p = profile([op('a', 'add', ['x', 0], unit_node=0),
                     dict(id='f', op='scale', args=['a'], integer=False, width=64, signed=True, factor=.5)],
                    baseline=[dict(node=0, unit='5RP')])
        self.assertEqual(ir.analyze(p, [0], BOUNDS)['error_bound'], 0)
        self.assertGreater(ir.analyze(p, [], BOUNDS)['error_bound'], 3)

    def test_validation_rejects_bad_graph_and_groups(self):
        p = profile([op('a', 'add', ['future', 0])])
        with self.assertRaisesRegex(ValueError, 'forward'):
            ir.validate_profile(p)
        p = profile([op('a', 'add', ['x', 0], unit_node=1)], baseline=[dict(node=2, unit='5RP')])
        with self.assertRaisesRegex(ValueError, 'Invalid'):
            ir.validate_profile(p)
        p = profile([op('a', 'scale', ['x'], factor=.5)])
        with self.assertRaisesRegex(ValueError, 'Fractional'):
            ir.validate_profile(p)

    def test_hundreds_of_nodes_have_bounded_evaluations(self):
        operations = []
        for index in range(400):
            operations.append(op(f'n{index}', 'add', ['x' if index == 0 else f'n{index-1}', 0], unit_node=index))
        p = profile(operations, baseline=[dict(node=i, unit='5QC') for i in range(400)])
        result = ir.plan(p, BOUNDS, 1, 10000, max_evaluations=20, max_candidates=10)
        self.assertLessEqual(result['analysis_evaluations'], 20)
        self.assertEqual(result['mode'], 'ir_internal')
        self.assertEqual(result['selection_method'], 'structural_greedy_v1')
        self.assertFalse(result['optimality_claim'])
        self.assertEqual(result['roi_mse_lower_bound'], 0)
        self.assertLessEqual((result['inside_error_bound']**2 + 9999*result['outside_error_bound']**2)/10000, 65.025)
        self.assertNotIn('combinations', inspect.getsource(ir))

    def test_no_certificate_is_not_claimed_infeasible(self):
        p = profile([op('a', 'add', ['x', 0])])
        result = ir.plan(p, {}, 100, 100, min_bit=11, max_evaluations=1)
        self.assertEqual(result['selection_status'], 'no_certificate_found')
        self.assertFalse(result['search_complete'])
        self.assertIn('not an infeasibility', result['reason'])

    def test_only_marked_adders_are_flip_candidates(self):
        p = profile([op('a', 'add', ['x', 0], unit_node=0),
                     op('final', 'clamp', ['a'], min=0, max=255)],
                    baseline=[dict(node=0, unit='exact')])
        result = ir.plan(p, {}, 1, 10000)
        self.assertEqual(result['mode'], 'ir_internal')
        self.assertEqual(result['node'], 'a')
        self.assertEqual(result['exact_nodes'], [])

    def test_matrix_resource_guard_precedes_allocation(self):
        p = {'ir': {'inputs': [None]*3000, 'operations': [], 'outputs': [None]*3000}}
        with self.assertRaisesRegex(ValueError, 'resource limit'):
            ir._transfer_gains(p)

    def test_sparse_hardware_group_ids(self):
        p = profile([op('a', 'add', ['x', 0], unit_node=10),
                     op('b', 'add', ['a', 0], unit_node=20)],
                    baseline=[dict(node=10, unit='5RP'), dict(node=20, unit='5QC')])
        ir.validate_profile(p)
        self.assertEqual(ir.analyze(p, [], BOUNDS)['error_bound'], 5)
        self.assertEqual(ir.analyze(p, [10], BOUNDS)['error_bound'], 1)
        result = ir.plan(p, BOUNDS, 4818, 102400)
        self.assertEqual(result['mode'], 'ir_internal')
        self.assertTrue(set(result['exact_nodes']) <= {10, 20})
        self.assertLessEqual((4818*result['inside_error_bound']**2 + (102400-4818)*result['outside_error_bound']**2)/102400, 65.025)


if __name__ == '__main__':
    unittest.main()
