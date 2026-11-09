"""Exercise the area choice and cropped-tile certificate with small graphs."""

import json
from pathlib import Path
import unittest

from test_circuit_ir import op, profile
from unit_policy import frame_weights, mse_bound, plan, with_units


CONFIGS = Path(__file__).resolve().parents[1] / 'configs'


class UnitPolicyTests(unittest.TestCase):
    def test_area_search_selects_lower_error_approximate_unit(self):
        graph = profile([op('a', 'add', ['x', 0], unit_node=0)],
                        baseline=[dict(node=0, unit='5TE')])
        bounds = json.loads((CONFIGS/'unit_error_bounds.json').read_text())['units']
        costs = json.loads((CONFIGS/'nangate45_unit_costs.json').read_text())
        choice = plan(graph, bounds, costs, 320, [120,135,193,201], 4, 128, 32, 0.5)
        self.assertEqual(choice['unit_replacements'], [dict(node=0, unit='5RP')])
        self.assertEqual(choice['exact_nodes'], [])
        self.assertLessEqual(choice['global_mse_bound'], 65.025)
        self.assertLessEqual(choice['analysis_evaluations'], 128)
        self.assertEqual(graph['baseline'][0]['unit'], '5TE')
        self.assertEqual(with_units(graph, choice['unit_replacements'])['baseline'][0]['unit'], '5RP')

    def test_cropped_tiles_charge_only_visible_background_positions(self):
        outputs = [dict(dx=x, dy=y) for y in range(2) for x in range(3)]
        graph = dict(ir=dict(tile=[2,3], outputs=outputs))
        weights = frame_weights(graph, 8, 5, [0,0,6,4])
        background = [1,2,3,4,5,6]
        inside = dict(rms=1, outputs=[1]*6)
        outside = dict(rms=10, outputs=background)
        brute = sum(1 if x < 6 and y < 4 else background[(y%2)*3+x%3]**2
                    for y in range(5) for x in range(8))/40
        self.assertAlmostEqual(mse_bound(weights, inside, outside), brute)
        outside['rms'] = 2
        # Four complete ROI tiles plus two right, two bottom, and one corner tile.
        clipped = (24 + 2*24 + 2*14 + 5)/40
        self.assertAlmostEqual(mse_bound(weights, inside, outside), clipped)


if __name__ == '__main__':
    unittest.main()
