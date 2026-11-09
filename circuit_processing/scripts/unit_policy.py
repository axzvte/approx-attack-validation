"""Bounded area-aware selection over the same typed graph for every kernel."""

from copy import deepcopy
import math

import circuit_ir


def read_costs(document):
    """Require a complete, positive unit-area table with explicit provenance."""
    if document.get('schema') != 1 or not document.get('source'):
        raise ValueError('Expected an area cost table with schema 1 and source')
    costs = document['unit_area_um2']
    required = (circuit_ir.UNITS - {'exact'}) | {'exact32', 'exact64'}
    if set(costs) != required or any(
        type(value) not in (int, float) or not math.isfinite(value) or not 0 < value < 1e9
        for value in costs.values()
    ):
        raise ValueError('Invalid or incomplete unit-area costs')
    return costs


def with_units(profile, replacements):
    """Copy the profile with static near-approximate replacements applied."""
    if not replacements:
        return profile
    result = deepcopy(profile)
    by_group = {item['node']: item for item in result['baseline']}
    seen = set()
    for replacement in replacements:
        group, unit = replacement['node'], replacement['unit']
        if group in seen or group not in by_group or by_group[group]['unit'] == 'exact':
            raise ValueError('Invalid approximate replacement group')
        if unit not in circuit_ir.UNITS - {'exact'}:
            raise ValueError('Invalid approximate replacement unit')
        seen.add(group)
        by_group[group]['unit'] = unit
    return result


def frame_weights(profile, width, height, box):
    """Count full flipped, full unflipped and cropped unflipped output tiles."""
    th, tw = profile['ir']['tile']
    l, t, r, b = box
    nx, rx = divmod(width, tw)
    ny, ry = divmod(height, th)
    fx = max(0, r//tw - (l+tw-1)//tw)
    fy = max(0, b//th - (t+th-1)//th)
    flipped = fx*fy
    partial = tuple((count, tuple(i for i, out in enumerate(profile['ir']['outputs'])
                                  if out['dx'] < cols and out['dy'] < rows))
                    for count, cols, rows in ((ny, rx, th), (nx, tw, ry), (1, rx, ry))
                    if count and cols and rows)
    return flipped, nx*ny-flipped, th*tw, partial, width*height


def mse_bound(weights, inside, outside):
    flipped, unflipped, pixels, partial, total = weights
    if not flipped:
        return math.inf
    outside_tile = pixels*outside['rms']**2
    # A cropped subset is also bounded by the total energy of its padded tile.
    cropped = math.fsum(count*min(outside_tile, math.fsum(outside['outputs'][i]**2 for i in visible))
                        for count, visible in partial)
    return math.fsum((flipped*pixels*inside['rms']**2,
                      unflipped*outside_tile, cropped))/total * (1+1e-12)


def configuration_mse(profile, choice, width, height, box):
    return mse_bound(frame_weights(profile, width, height, box),
                     choice['inside_certificate'], choice['outside_certificate'])


def plan(profile, bounds, costs, grid, box, min_bit, max_evaluations, max_candidates,
         strength_ratio):
    """Compare a bounded set of flips and greedily reduce certified unit area."""
    import circuit_energy
    circuit_ir.validate_profile(profile)
    costs = read_costs(costs)
    if type(strength_ratio) not in (int, float) or not 0 < strength_ratio <= 1:
        raise ValueError('Minimum structural strength ratio must be in (0, 1]')
    weights = frame_weights(profile, grid, grid, box)
    if not weights[0]:
        raise ValueError('No complete tile is inside the frozen ROI')
    energy = circuit_energy.prepare(profile)
    baseline = {x['node']: x['unit'] for x in profile['baseline']}
    groups = sorted(g for g, unit in baseline.items() if unit != 'exact')
    widths = {}
    for op in profile['ir']['operations']:
        if 'unit_node' in op:
            group = op['unit_node']
            widths[group] = max(widths.get(group, 0), op['width'])

    def cost(group, unit):
        return costs[f'exact{widths[group]}' if unit == 'exact' else unit]

    def total_cost(assignment):
        return math.fsum(cost(group, assignment.get(group, unit))
                         for group, unit in sorted(baseline.items()))

    def wce(unit):
        return 0 if unit == 'exact' else circuit_ir._unit_bound(unit, bounds)[2]

    options = {g: [unit for unit in sorted(circuit_ir.UNITS)
                   if wce(unit) <= wce(baseline[g])] for g in groups}
    trace = []
    cache = {}

    def evaluate(assignment, flip, cap):
        key = (tuple(sorted(assignment.items())), flip)
        if key in cache:
            return cache[key]
        if len(cache) >= cap:
            return None
        exact = [g for g, unit in assignment.items() if unit == 'exact']
        changes = [dict(node=g, unit=unit) for g, unit in assignment.items()
                   if unit != 'exact' and unit != baseline[g]]
        effective = with_units(profile, changes)
        result = circuit_ir._analyze(effective, exact, bounds, flip, details=True)
        cert = False
        if not result['invalid_reason']:
            output = result['output_bounds']
            rms = math.hypot(*output)/math.sqrt(len(output))
            method = 'per_output'
            tighter = energy.rms_bound(result['state']) if energy is not None else None
            if tighter is not None and tighter < rms:
                rms, method = tighter, 'linear_tail_energy'
            cert = dict(outputs=output, rms=rms, max=result['error_bound'], method=method)
        cache[key] = cert
        return cert

    exact = dict.fromkeys(groups, 'exact')
    outside = evaluate(exact, None, max_evaluations)
    if not outside:
        raise ValueError('All-precise graph failed range analysis')
    gains = circuit_ir._transfer_gains(profile)
    influence = dict.fromkeys(groups, 0.0)
    sites = []
    for op in profile['ir']['operations']:
        group = op.get('unit_node')
        if group is None:
            continue
        values = gains[op['id']]
        gain = math.hypot(*(float(x) for x in values))/math.sqrt(len(values))
        if group in influence:
            influence[group] += gain
        if gain > 0 and op['integer']:
            sites.append((gain, op['id']))
    sites.sort(key=lambda item: (-item[0], item[1]))

    # Interleave bits so high-scoring sites at one bit cannot consume the shortlist.
    trials = 0
    best_by_bit = {}
    candidate_cap = min(max_candidates, max(1, max_evaluations//4))
    for gain, site in sites:
        for bit in range(11, min_bit-1, -1):
            if trials >= candidate_cap:
                break
            if bit in best_by_bit:
                continue
            trials += 1
            flip = (site, bit)
            inside = evaluate(exact, flip, max_evaluations)
            if inside and mse_bound(weights, inside, outside) <= 65.025:
                best_by_bit[bit] = (gain*(1 << bit), flip, inside)
        if trials >= candidate_cap or len(best_by_bit) == 12-min_bit:
            break
    if not best_by_bit:
        raise ValueError('No certified internal flip in the structural candidate budget')
    peak = max(x[0] for x in best_by_bit.values())
    shortlist = sorted((x for x in best_by_bit.values() if x[0] >= peak*strength_ratio),
                       key=lambda x: (-x[0], x[1]))
    best = None
    for i, (score, flip, inside) in enumerate(shortlist):
        cap = len(cache) + (max_evaluations-len(cache))//(len(shortlist)-i)
        current = exact.copy()
        bg, fg = outside, inside
        while len(cache) < cap:
            moves = []
            for g in groups:
                for unit in options[g]:
                    saving = cost(g, current[g])-cost(g, unit)
                    if saving > 0:
                        added = influence[g]*max(0, wce(unit)-wce(current[g]))
                        moves.append((-saving/(1+added), -saving, g, unit))
            for _, _, group, unit in sorted(moves):
                trial = current.copy()
                trial[group] = unit
                next_bg = evaluate(trial, None, cap)
                if not next_bg:
                    continue
                next_fg = evaluate(trial, flip, cap)
                if next_fg and mse_bound(weights, next_fg, next_bg) <= 65.025:
                    current, bg, fg = trial, next_bg, next_fg
                    break
            else:
                break
        area = total_cost(current)
        mse = mse_bound(weights, fg, bg)
        trace.append(dict(node=flip[0], bit=flip[1], structural_score=score,
                          unit_area_um2=area, mse_bound=mse))
        key = (area, -score, flip)
        if best is None or key < best[0]:
            best = (key, current, fg, bg, mse)

    key, final, inside, outside, mse = best
    site, bit = key[2]
    return dict(mode='ir_internal', node=site, bit=bit,
                exact_nodes=[g for g in groups if final[g] == 'exact'],
                unit_replacements=[dict(node=g, unit=final[g]) for g in groups
                                   if final[g] not in ('exact', baseline[g])],
                inside_error_bound=max(inside['max'], outside['max']),
                outside_error_bound=outside['max'],
                inside_certificate=inside, outside_certificate=outside,
                global_mse_bound=mse,
                structural_score=-key[1], structural_score_floor=peak*strength_ratio,
                unit_area_um2=key[0], baseline_unit_area_um2=total_cost({}),
                analysis_evaluations=len(cache), candidate_count_studied=trials,
                selection_method='area_aware_units_v1', selection_trace=trace,
                roi_mse_lower_bound=0, roi_effect='requires_image_verification',
                optimality_claim=False, search_complete=False)
