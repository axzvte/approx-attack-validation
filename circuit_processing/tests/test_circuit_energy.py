"""Whole-tile error certificates on synthetic scalar IR."""
import math
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import circuit_energy
import circuit_ir


def op(name, kind, args, integer=False, **extra):
    return dict(id=name, op=kind, args=args, integer=integer, width=64, signed=True, **extra)


def graph(ops, outputs, baseline=()):
    return dict(schema=3, application='ir', name='synthetic', baseline=list(baseline),
                ir=dict(schema=1, tile=[1, len(outputs)], border='replicate', interior_only=False,
                        inputs=[dict(id='x', dx=0, dy=0), dict(id='y', dx=1, dy=0)],
                        operations=ops,
                        outputs=[dict(value=v, dx=i, dy=0) for i, v in enumerate(outputs)]))


def analyze(profile, flip=None):
    return circuit_ir._analyze(profile, set(), {'5RP': 4}, flip, details=True)['state']


class CircuitEnergyTests(unittest.TestCase):
    def test_signed_coefficients_preserve_shared_value_cancellation(self):
        profile = graph([
            op('a', 'add', ['x', 0], integer=True, unit_node=0),
            op('b', 'scale', ['a'], factor=0.125),
            op('out', 'sub', ['b', 'b']),
        ], ['out'], [dict(node=0, unit='5RP')])
        cert = circuit_energy.prepare(profile)
        self.assertLess(cert.rms_bound(analyze(profile, ('a', 5))), 1.001)
        exact = circuit_ir._analyze(profile, {0}, {}, None, details=True)['state']
        self.assertEqual(cert.rms_bound(exact), 0)

    def test_gram_accounts_for_mixing_and_output_rounding(self):
        profile = graph([
            op('a', 'add', ['x', 0], integer=True),
            op('b', 'add', ['y', 0], integer=True, unit_node=0),
            op('sum', 'add', ['a', 'b']),
            op('diff', 'sub', ['a', 'b']),
            op('r', 'round_even', ['sum'], integer=True),
            op('id', 'scale', ['r'], integer=True, factor=1),
            op('out', 'clamp', ['id'], min=0.5, max=254.5),
        ], ['out', 'diff'], [dict(node=0, unit='5RP')])
        cert = circuit_energy.prepare(profile)
        state = analyze(profile, ('a', 3))
        bound = cert.rms_bound(state)
        self.assertGreaterEqual(bound, math.sqrt(8**2 + 4**2))
        self.assertLess(bound, 10)
        self.assertIsNone(cert.rms_bound(analyze(profile, ('r', 3))))
        self.assertIsNone(cert.rms_bound(analyze(profile, ('id', 3))))

    def test_unsupported_terminal_nonlinearity_fails_closed(self):
        profile = graph([op('a', 'scale', ['x'], factor=0.125), op('out', 'abs', ['a'])], ['out'])
        self.assertIsNone(circuit_energy.prepare(profile))

    def test_floating_roundoff_can_break_algebraic_cancellation(self):
        offset, factor = 2**39, 0.00008
        profile = graph([
            op('a', 'add', ['x', 0], integer=True, unit_node=0),
            op('v', 'scale', ['a'], factor=factor),
            op('large', 'add', ['v', offset]),
            op('small', 'sub', ['large', offset]),
            op('cancel', 'sub', ['small', 'v']),
            op('out', 'add', ['cancel', 0.5]),
        ], ['out'], [dict(node=0, unit='5RP')])

        def execute(value):
            v = value * factor
            return round(((v + offset) - offset - v) + 0.5)

        self.assertEqual(abs(execute(1) - execute(2)), 1)
        bound = circuit_energy.prepare(profile).rms_bound(analyze(profile))
        self.assertGreater(bound, 1.00000001)

    def test_nonfinite_frontier_state_fails_closed(self):
        profile = graph([op('out', 'scale', ['x'], factor=0.125)], ['out'])
        state = analyze(profile)
        state['x'] = (0, 255, math.inf, True)
        self.assertIsNone(circuit_energy.prepare(profile).rms_bound(state))


if __name__ == '__main__':
    unittest.main()
