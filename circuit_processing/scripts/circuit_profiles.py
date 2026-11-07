"""Explicit circuit inputs and image-independent interval/error analysis."""
from __future__ import annotations

import itertools
import re

UNITS = ('exact', '5QC', '5QT', '5L8', '5RP', '5TE', '5PD', '5PN', '5SB', '5Z0')
NODE_COUNTS = {'sharpen': 4, 'conv': 8, 'sobel': 11, 'dct': 32}
DEFAULT_APPROXIMATE = {
    'sharpen': (0, 1, 2, 3), 'conv': (1, 2, 4, 7),
    'sobel': (0, 2, 4, 7, 9), 'dct': (8, 10, 11, 13, 16, 19, 22, 25, 28, 31),
}
INTEGER_LIMIT = 2**30 - 1


def integer(value, name, low=-INTEGER_LIMIT, high=INTEGER_LIMIT):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{name} must be an integer in [{low}, {high}]')
    return value


def default_profile(application):
    return dict(schema=1, name=application, application=application,
                baseline=[dict(node=node, unit='5RP' if node in DEFAULT_APPROXIMATE[application] else 'exact')
                          for node in range(NODE_COUNTS[application])])


def validate_graph(graph):
    if not isinstance(graph, dict) or graph.get('border') not in ('replicate', 'reflect101'):
        raise ValueError('Graph needs a replicate or reflect101 border rule')
    inputs = graph.get('inputs')
    operations = graph.get('operations')
    if not isinstance(inputs, dict) or not inputs or not isinstance(operations, list) or not operations:
        raise ValueError('Graph needs nonempty inputs and operations')
    known = set()
    for name, binding in inputs.items():
        if not isinstance(name, str) or not name or name.isdecimal() or not isinstance(binding, dict):
            raise ValueError('Input names must be nonnumeric strings with pixel bindings')
        if set(binding) - {'dx', 'dy', 'scale', 'bias'}:
            raise ValueError(f'Unknown input binding field: {name}')
        integer(binding.get('dx', 0), 'dx', -1024, 1024)
        integer(binding.get('dy', 0), 'dy', -1024, 1024)
        scale = integer(binding.get('scale', 1), 'scale')
        bias = integer(binding.get('bias', 0), 'bias')
        if min(bias, 255*scale+bias) < -INTEGER_LIMIT or max(bias, 255*scale+bias) > INTEGER_LIMIT:
            raise ValueError(f'Input binding exceeds supported integer range: {name}')
        known.add(name)
    arithmetic = []
    fields = {'add': set(), 'sub': set(), 'abs': set(), 'scale': {'factor'},
              'shift': {'bits'}, 'clamp': {'min', 'max'}}
    for operation in operations:
        if not isinstance(operation, dict):
            raise ValueError('Every operation must be an object')
        name, kind, operands = operation.get('id'), operation.get('op'), operation.get('args')
        if not isinstance(name, str) or not name or name in known or kind not in fields:
            raise ValueError(f'Invalid/duplicate operation or unsupported operator: {name}')
        if set(operation) != {'id', 'op', 'args'} | fields[kind]:
            raise ValueError(f'Unexpected/missing fields for operation {name}')
        if not isinstance(operands, list) or len(operands) != (2 if kind in ('add', 'sub') else 1):
            raise ValueError(f'Wrong operand count: {name}')
        for operand in operands:
            if isinstance(operand, str):
                if operand not in known:
                    raise ValueError(f'Unknown or forward operand {operand} at {name}')
            else:
                integer(operand, f'operand at {name}')
        if kind in ('add', 'sub'):
            if name != str(len(arithmetic)):
                raise ValueError('Add/sub node IDs must be consecutive strings starting at "0"')
            arithmetic.append(int(name))
        elif name.isdecimal():
            raise ValueError('Nonarithmetic operation IDs must be names')
        if kind == 'scale':
            integer(operation['factor'], 'factor')
        elif kind == 'shift':
            integer(operation['bits'], 'bits', 0, 20)
        elif kind == 'clamp':
            integer(operation['min'], 'clamp min')
            integer(operation['max'], 'clamp max')
            if operation['min'] > operation['max']:
                raise ValueError('clamp min exceeds max')
        known.add(name)
    if graph.get('output') not in known or not arithmetic:
        raise ValueError('Graph needs an existing output and at least one add/sub node')
    if error_bound(graph, {}) is None:
        raise ValueError('Exact graph exceeds the supported integer range')
    return arithmetic


def validate_profile(profile):
    if not isinstance(profile, dict) or profile.get('schema') != 1:
        raise ValueError('Expected circuit profile schema 1')
    if not isinstance(profile.get('name'), str) or not re.fullmatch(r'[A-Za-z0-9_-]+', profile['name']):
        raise ValueError('Circuit name must contain letters, digits, underscores or hyphens')
    app = profile.get('application')
    if app not in (*NODE_COUNTS, 'graph'):
        raise ValueError(f'Unknown application: {app}')
    if set(profile) != {'schema', 'name', 'application', 'baseline'} | ({'graph'} if app == 'graph' else set()):
        raise ValueError('Unexpected or missing circuit profile fields')
    nodes = validate_graph(profile.get('graph')) if app == 'graph' else list(range(NODE_COUNTS[app]))
    baseline = profile.get('baseline')
    if not isinstance(baseline, list) or len(baseline) != len(nodes):
        raise ValueError('Provide exactly one baseline assignment for every add/sub node')
    assigned = set()
    for item in baseline:
        if not isinstance(item, dict) or set(item) != {'node', 'unit'}:
            raise ValueError('Baseline assignment must contain node and unit')
        node = integer(item['node'], 'node', 0, len(nodes)-1)
        if node in assigned or item['unit'] not in UNITS:
            raise ValueError('Duplicate node or unknown adder unit')
        assigned.add(node)
    return profile


def graph_from_tuples(graph):
    """Convert the audited built-in graphs; range operands stay explicit internally."""
    return dict(inputs={}, operations=[dict(id=node, op=kind, args=operands)
                for node, kind, *operands in graph], output=graph[-1][0])


def error_bound(graph, errors, flip=None):
    state = {}
    for name, binding in graph['inputs'].items():
        scale, bias = binding.get('scale', 1), binding.get('bias', 0)
        state[name] = (min(bias, 255*scale+bias), max(bias, 255*scale+bias), 0)
    def operand(value):
        if isinstance(value, str):
            return state[value]
        if isinstance(value, (tuple, list)):
            return value[0], value[1], 0
        return value, value, 0
    for operation in graph['operations']:
        name, kind = operation['id'], operation['op']
        operands = [operand(value) for value in operation['args']]
        lo, hi, error = operands[0]
        if kind in ('add', 'sub'):
            other_lo, other_hi, other_error = operands[1]
            if kind == 'sub':
                other_lo, other_hi = -other_hi, -other_lo
            local = errors.get(name, 0)
            if name in errors and (lo < -2048 or hi > 2047 or other_lo < -2048 or other_hi > 2047):
                return None
            lo, hi, error = lo+other_lo-local, hi+other_hi+local, error+other_error+local
        elif kind == 'abs':
            lo, hi = (0 if lo <= 0 <= hi else min(abs(lo), abs(hi))), max(abs(lo), abs(hi))
        elif kind == 'scale':
            factor = operation['factor']
            lo, hi, error = min(lo*factor, hi*factor), max(lo*factor, hi*factor), error*abs(factor)
        elif kind == 'shift':
            divisor = 1 << operation['bits']
            lo, hi, error = lo//divisor, hi//divisor, (error+divisor-1)//divisor
        elif kind == 'clamp':
            lower, upper = operation['min'], operation['max']
            lo, hi = max(lower, min(upper, lo)), max(lower, min(upper, hi))
            error = min(error, upper-lower)
        else:
            raise ValueError(f'Unsupported operation: {kind}')
        if flip and name == str(flip[0]):
            if lo < -4096 or hi > 4095:
                return None
            delta = 1 << flip[1]
            lo, hi, error = lo-delta, hi+delta, error+delta
        if lo < -INTEGER_LIMIT or hi > INTEGER_LIMIT:
            return None
        state[name] = lo, hi, error
    return min(255, state[graph['output']][2])


def distances_to_output(graph):
    distance = {graph['output']: 0}
    for operation in reversed(graph['operations']):
        if operation['id'] in distance:
            for operand in operation['args']:
                if isinstance(operand, str):
                    distance[operand] = max(distance.get(operand, 0), distance[operation['id']]+1)
    return distance


def plan(profile, graph, unit_bounds, area, total, min_bit, within_budget, max_subsets=10000):
    assignments = {item['node']: item['unit'] for item in profile['baseline']}
    approximate = tuple(sorted(node for node, unit in assignments.items() if unit != 'exact'))
    common = dict(roi_mse_lower_bound=0, roi_effect='requires_image_verification')
    if profile['application'] == 'dct':
        for bit in range(11, max(4, min_bit+3)-1, -1):
            bound = 1 << (bit-3)
            if within_budget(area, total, bound, 0):
                return dict(mode='internal_dc', exact_nodes=list(approximate), node=10, bit=bit,
                            inside_error_bound=bound, outside_error_bound=0,
                            scope='column_pass_frequency_0_full_blocks', **common)
    else:
        errors = {str(node): unit_bounds[assignments[node]]['wce'] for node in approximate}
        if error_bound(graph, errors) is None:
            raise ValueError(f"{profile['name']}: baseline operand ranges cannot be certified for signed12 adders")
        distances = distances_to_output(graph)
        flip_nodes = [node for node in (approximate or tuple(assignments)) if str(node) in distances]
        best = None
        examined = 0
        exhausted = False
        # A fully exact compensated candidate supplies a deterministic bound
        # even when subset enumeration reaches the configured search limit.
        subsets = itertools.chain([approximate],
            (subset for count in range(len(approximate)) for subset in itertools.combinations(approximate, count)))
        for replaced in subsets:
            if examined >= max_subsets:
                exhausted = True
                break
            examined += 1
            kept = {node: error for node, error in errors.items() if int(node) not in replaced}
            outside = error_bound(graph, kept)
            if outside is None or not within_budget(area, total, outside, outside):
                continue
            for node in flip_nodes:
                for bit in range(min_bit, 12):
                    inside = error_bound(graph, kept, (node, bit))
                    if inside is None or not within_budget(area, total, inside, outside):
                        continue
                    rank = (len(replaced), -bit, distances[str(node)], -node, replaced)
                    choice = dict(mode='internal', exact_nodes=list(replaced), node=node, bit=bit,
                                  inside_error_bound=inside, outside_error_bound=outside,
                                  downstream_ops_before_clip=distances[str(node)], **common)
                    if best is None or rank < best[0]:
                        best = rank, choice
        if best:
            return dict(**best[1], examined_subsets=examined, search_exhaustive=not exhausted)
    for bit in range(7, min_bit-1, -1):
        if within_budget(area, total, 1 << bit, 0):
            return dict(mode='output_fallback', exact_nodes=list(approximate), node=-1, bit=bit,
                        inside_error_bound=1 << bit, outside_error_bound=0,
                        roi_mse_lower_bound=(1 << bit)**2, roi_effect='relative_to_exact_output',
                        reason='No certified internal candidate found within the configured search')
    raise ValueError(f"{profile['name']}: no certified configuration found under the current constraints")
