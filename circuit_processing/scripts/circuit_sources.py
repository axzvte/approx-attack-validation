"""Load source-backed kernels and migrate the earlier explicit graph interfaces."""
from __future__ import annotations

import copy
import json
from pathlib import Path

import circuit_ir
import legacy_circuit_profiles as legacy

ROOT = Path(__file__).resolve().parents[1]


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def encode_source(descriptor, base_dir):
    try:
        from c_kernel_encoder import encode_profile
    except ModuleNotFoundError as error:
        if error.name == 'pycparser':
            raise ValueError('Install circuit dependencies: python -m pip install -r circuit_processing/requirements.txt') from error
        raise
    return circuit_ir.validate_profile(encode_profile(descriptor, base_dir))


def builtin_source(profile):
    interface = ROOT/'kernels'/f"{profile['application']}_interface.json"
    descriptor = read_json(interface)
    descriptor['name'] = profile['name']
    descriptor['baseline'] = profile['baseline']
    return encode_source(descriptor, interface.parent)


def migrate_graph(profile):
    """Preserve existing hand-authored stencil inputs under the shared IR."""
    graph = profile['graph']
    inputs, operations, references = [], [], {}
    for index, (name, binding) in enumerate(graph['inputs'].items()):
        original = f'input_{index}'
        inputs.append(dict(id=original, dx=binding.get('dx', 0), dy=binding.get('dy', 0)))
        value = original
        for kind, parameter, amount in (('scale', 'factor', binding.get('scale', 1)),
                                        ('add', None, binding.get('bias', 0))):
            if (kind == 'scale' and amount == 1) or (kind == 'add' and amount == 0):
                continue
            result = f'input_{index}_{kind}'
            operation = dict(id=result, op=kind, args=[value], integer=True, width=64, signed=True)
            if parameter:
                operation[parameter] = amount
            else:
                operation['args'].append(amount)
            operations.append(operation)
            value = result
        references[name] = value
    for index, item in enumerate(graph['operations']):
        operation = copy.deepcopy(item)
        operation['id'] = f'op_{index}'
        operation['args'] = [references[value] if isinstance(value, str) else value for value in item['args']]
        operation.update(integer=True, width=64, signed=True)
        if item['op'] in ('add', 'sub'):
            operation['unit_node'] = int(item['id'])
        operations.append(operation)
        references[item['id']] = operation['id']
    result = dict(schema=3, name=profile['name'], application='ir', baseline=copy.deepcopy(profile['baseline']),
                  ir=dict(schema=1, tile=[1, 1], border=graph['border'], interior_only=False, border_output='zero',
                          inputs=inputs, operations=operations,
                          outputs=[dict(value=references[graph['output']], dx=0, dy=0)],
                          source=dict(kind='legacy_explicit_graph')))
    return circuit_ir.validate_profile(result)


def load_profile(path):
    path = Path(path).expanduser().resolve()
    profile = read_json(path)
    if profile.get('schema') == 3:
        return circuit_ir.validate_profile(profile)
    if profile.get('application') == 'c':
        return encode_source(profile, path.parent)
    legacy.validate_profile(profile)
    if profile['application'] == 'graph':
        return migrate_graph(profile)
    return circuit_ir.validate_profile(builtin_source(profile))
