"""Historical schema1 planning, used only to replay frozen old experiments."""
import itertools

ORIGINAL_5RP = {
    "sharpen": (0, 1, 2, 3),
    "conv": (1, 2, 4, 7),
    "sobel": (0, 2, 4, 7, 9),
    "dct": (8, 10, 11, 13, 16, 19, 22, 25, 28, 31),
}
PEAK_SQUARED = 255**2
WCE_5RP = 4


def spatial_graph(app):
    p, neg, twice = (0, 255), (-255, 0), (0, 510)
    if app == "sharpen":
        return [
            ("0", "add", (0, 1275), neg),
            ("1", "add", "0", neg),
            ("2", "add", "1", neg),
            ("3", "add", "2", neg),
        ]
    if app == "conv":
        n = (-127, 128)
        return [
            ("0", "add", n, n), ("1", "add", n, n),
            ("2", "add", (-1024, 1016), n), ("3", "add", n, n),
            ("4", "add", "0", "1"), ("5", "add", "2", "3"),
            ("6", "add", "4", "5"), ("7", "add", "6", n),
            ("magnitude", "abs", "7"),
        ]
    if app == "sobel":
        return [
            ("0", "add", p, twice), ("1", "add", "0", p),
            ("2", "add", p, twice), ("3", "add", "2", p),
            ("4", "sub", "1", "3"),
            ("5", "add", p, twice), ("6", "add", "5", p),
            ("7", "add", p, twice), ("8", "add", "7", p),
            ("9", "sub", "6", "8"),
            ("ax", "abs", "4"), ("ay", "abs", "9"),
            ("10", "add", "ax", "ay"),
        ]
    raise ValueError(f"No internal error model for {app}")


def output_error_bound(graph, approximate, flip=None):
    """Propagate value intervals and error bounds, including signed input limits."""
    state = {}
    for node, operation, *inputs in graph:
        operands = [state[x] if isinstance(x, str) else (*x, 0) for x in inputs]
        lo, hi, error = operands[0]
        if operation == "abs":
            state[node] = (0 if lo <= 0 <= hi else min(abs(lo), abs(hi)),
                           max(abs(lo), abs(hi)), error)
            continue
        if node in approximate and any(a < -2048 or b > 2047 for a, b, _ in operands):
            return None
        other_lo, other_hi, other_error = operands[1]
        if operation == "sub":
            other_lo, other_hi = -other_hi, -other_lo
            if node in approximate and (other_lo < -2048 or other_hi > 2047):
                return None
        local = WCE_5RP if node in approximate else 0
        lo, hi = lo + other_lo - local, hi + other_hi + local
        error += other_error + local
        if flip and node == flip[0]:
            if lo < -4096 or hi > 4095:
                return None
            delta = 1 << flip[1]
            lo, hi, error = lo-delta, hi+delta, error+delta
        state[node] = lo, hi, error
    # Saturation to a byte cannot increase the difference.
    return min(255, state[node][2])


def within_budget(area, total, inside_error, outside_error):
    return 1000 * (area*inside_error**2 + (total-area)*outside_error**2) <= PEAK_SQUARED*total


def plan_circuit(app, area, total, min_bit):
    original = ORIGINAL_5RP[app]
    if app != "dct":
        graph = spatial_graph(app)
        # For equal certified choices prefer the shorter remaining path,
        # since additional operators can suppress an internal flip.
        downstream = {graph[-1][0]: 0}
        for node, _, *inputs in reversed(graph):
            for operand in inputs:
                if isinstance(operand, str):
                    downstream[operand] = max(downstream.get(operand, 0), downstream[node]+1)
        for count in range(len(original)+1):
            candidates = []
            for replaced in itertools.combinations(original, count):
                kept = set(map(str, set(original)-set(replaced)))
                outside = output_error_bound(graph, kept)
                if outside is None or not within_budget(area, total, outside, outside):
                    continue
                for node in original:
                    for bit in range(min_bit, 12):
                        inside = output_error_bound(graph, kept, (str(node), bit))
                        if inside is not None and within_budget(area, total, inside, outside):
                            candidates.append((-bit, downstream[str(node)], -node, replaced, inside, outside))
            if candidates:
                neg_bit, distance, neg_node, replaced, inside, outside = min(candidates)
                return dict(mode="internal", exact_nodes=list(replaced), node=-neg_node,
                            bit=-neg_bit, inside_error_bound=inside, outside_error_bound=outside,
                            downstream_ops_before_clip=distance,
                            roi_mse_lower_bound=0, roi_effect="requires_image_verification")
    else:
        # An isolated terminal coefficient in the final transform pass
        # contributes 1/8 of its change to each of the block's pixels.
        for bit in range(11, max(4, min_bit+3)-1, -1):
            bound = 1 << (bit-3)
            if within_budget(area, total, bound, 0):
                return dict(mode="internal_dc", exact_nodes=list(original), node=10,
                            bit=bit, inside_error_bound=bound, outside_error_bound=0,
                            scope="column_pass_frequency_0_full_blocks",
                            roi_mse_lower_bound=0, roi_effect="requires_image_verification")
    # Exact arithmetic makes the output bit's effect independent of the
    # upstream graph, including block transforms and reconstruction.
    for bit in range(7, min_bit-1, -1):
        if within_budget(area, total, 1 << bit, 0):
            return dict(mode="output_fallback", exact_nodes=list(original), node=-1,
                        bit=bit, inside_error_bound=1 << bit, outside_error_bound=0,
                        roi_mse_lower_bound=(1 << bit)**2,
                        roi_effect="relative_to_exact_output",
                        reason="no internal candidate within configured bounds")
    raise ValueError(f"{app}: no configuration fits this rectangle and minimum flip bit")
