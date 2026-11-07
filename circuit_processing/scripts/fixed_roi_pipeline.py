#!/usr/bin/env python3
"""Freeze a configuration from arithmetic bounds, then verify it on images."""
from __future__ import annotations

import argparse
import csv
import hashlib
import itertools
import json
import math
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
APPLICATIONS = ("sharpen", "conv", "sobel", "dct")
ORIGINAL_5RP = {
    "sharpen": (0, 1, 2, 3),
    "conv": (1, 2, 4, 7),
    "sobel": (0, 2, 4, 7, 9),
    "dct": (8, 10, 11, 13, 16, 19, 22, 25, 28, 31),
}
PEAK_SQUARED = 255**2
WCE_5RP = 4


# The operands follow the numbered calls in src/applications/*.cpp.
# Tuples of ints are input ranges. Strings refer to preceding values.
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


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_identity():
    paths = [Path(__file__), ROOT/"app/fixed_roi_verify.cpp",
             ROOT/"include/core/output_bitflip.hpp",
             ROOT/"src/approximate/evoapprox_adapter.cpp",
             ROOT/"third_party/evoapproxlib/add12se_5RP.c"]
    paths.extend(ROOT/f"src/applications/{app}.cpp" for app in (*APPLICATIONS, "dct8_fixed_graph"))
    return {path.relative_to(ROOT).as_posix(): hashlib.sha256(
        path.read_text(encoding="utf-8").encode("utf-8")
    ).hexdigest() for path in paths}


def checked_sources():
    sources = source_identity()
    model_sources = read_json(ROOT/"configs/fixed_roi_model_sources.json")
    for path, digest in model_sources.items():
        if sources.get(path) != digest:
            raise ValueError(f"Review the arithmetic model before using changed source: {path}")
    return sources


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, value):
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False, allow_nan=False)
        stream.write("\n")


def freeze(args):
    protocol = read_json(args.roi_run/"protocol.json")
    if (protocol["status"], protocol["feature"], protocol["coordinate_mode"]) != ("completed", "rc", "stretch"):
        raise ValueError("Expected a completed RC union run in stretch coordinates")
    grid = protocol["grid"]
    rows = read_json(args.roi_run/"rectangles.json")
    matches = [r for r in rows if r["method"] == "union" and r["frequency_percent"] == args.threshold]
    if len(matches) != 1 or matches[0]["bbox_half_open"] is None:
        raise ValueError("Threshold must identify a single nonempty union rectangle")
    bounds = matches[0]["bbox_half_open"]
    left, top, right, bottom = bounds
    if not 0 <= left < right <= grid or not 0 <= top < bottom <= grid:
        raise ValueError("Rectangle coordinates are outside the statistical grid")
    area = (right-left)*(bottom-top)
    config = dict(schema=1, grid=grid, bbox_half_open=bounds, frequency_percent=args.threshold,
                  roi_images=protocol["count"], roi_run_sha256=sha256(args.roi_run/"rectangles.json"),
                  min_bit=args.min_bit, sources=checked_sources(),
                  circuits={app: plan_circuit(app, area, grid*grid, args.min_bit) for app in APPLICATIONS})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_json(args.output, config)
    for app, choice in config["circuits"].items():
        print(f"{app}: {choice['mode']}, node {choice['node']}, bit {choice['bit']}, exact {choice['exact_nodes']}")


def project(bounds, grid, width, height):
    # A native pixel belongs to the rectangle if its center maps into it.
    left, top, right, bottom = bounds
    def edge(coord, length):
        return (2*coord*length + grid - 1) // (2*grid)
    return edge(left, width), edge(top, height), edge(right, width), edge(bottom, height)


def evaluate(args):
    import numpy as np
    from PIL import Image

    config = read_json(args.config)
    if config["schema"] != 1 or config["sources"] != checked_sources():
        raise ValueError("Frozen configuration belongs to a different source revision")
    grid, bounds = config["grid"], config["bbox_half_open"]
    l, t, r, b = bounds
    expected = {app: plan_circuit(app, (r-l)*(b-t), grid**2, config["min_bit"]) for app in APPLICATIONS}
    if config["circuits"] != expected:
        raise ValueError("Frozen circuit configuration differs from the code-derived result")
    image_dir = args.images.expanduser().resolve()
    output = args.output.resolve()
    if output.is_relative_to(image_dir) or image_dir.is_relative_to(output):
        raise ValueError("Output and input image directories must be separate")
    if not args.binary.is_file():
        raise ValueError(f"Missing compiled verifier: {args.binary}")
    if args.start < 1 or args.count < 1 or args.save_examples < 0:
        raise ValueError("Invalid image range or example count")
    inputs = [image_dir/f"{index}.jpg" for index in range(args.start, args.start+args.count)]
    for path in inputs:
        if not path.is_file():
            raise ValueError(f"Missing input image: {path}")
    output.mkdir(parents=True, exist_ok=False)
    rows = []
    failures = []
    applications = args.apps or APPLICATIONS
    with tempfile.TemporaryDirectory(dir=output) as scratch:
        scratch = Path(scratch)
        for index, path in enumerate(inputs):
            try:
                with Image.open(path) as image:
                    width, height = image.size
            except (OSError, ValueError) as error:
                for app in applications:
                    rows.append(dict(image=path.name, application=app, mode=config["circuits"][app]["mode"],
                                     status="failed", global_pass=False, roi_decreased=False,
                                     roi_decreased_vs_compensated=False))
                    failures.append(dict(image=path.name, application=app, error=str(error)))
                continue
            box = project(bounds, grid, width, height)
            left, top, right, bottom = box
            area, total = (right-left)*(bottom-top), width*height
            for app in applications:
                plan = config["circuits"][app]
                row = dict(image=path.name, application=app, mode=plan["mode"],
                           left=left, top=top, right=right, bottom=bottom,
                           status="failed", global_pass=False, roi_decreased=False,
                           roi_decreased_vs_compensated=False)
                rows.append(row)
                try:
                    if area == 0 or not within_budget(area, total, plan["inside_error_bound"], plan["outside_error_bound"]):
                        raise ValueError("Projected rectangle is empty or exceeds the frozen error budget")
                    destination = scratch
                    if index < args.save_examples:
                        destination = output/"examples"/app/path.stem
                        destination.mkdir(parents=True)
                    subprocess.run([str(args.binary.resolve()), app, str(path), str(destination),
                                    *map(str, box), str(plan["node"]), str(plan["bit"]),
                                    *map(str, plan["exact_nodes"])], check=True, capture_output=True, text=True)
                    def pixels(name):
                        with Image.open(destination/f"{name}.png") as im:
                            return np.asarray(im, dtype=np.int32)
                    reference = pixels("exact")
                    mask = np.zeros((height, width), dtype=bool)
                    mask[top:bottom, left:right] = True
                    compensated = pixels("compensated")
                    errors = {}
                    for name in ("baseline", "compensated", "processed"):
                        result = compensated if name == "compensated" else pixels(name)
                        if result.shape != (height, width) or reference.shape != result.shape:
                            raise ValueError("Verifier returned an unexpected image size")
                        delta = result-reference
                        squares = delta**2
                        errors[name] = (int(squares.sum()), int(squares[mask].sum()))
                        for label, sse, count in (("global", errors[name][0], total),
                                                  ("roi", errors[name][1], area),
                                                  ("background", errors[name][0]-errors[name][1], total-area)):
                            row[f"{name}_{label}_psnr"] = ("" if not count else "inf" if not sse
                                                          else 10*math.log10(PEAK_SQUARED*count/sse))
                        if name == "processed" and (
                            np.any(np.abs(delta[mask]) > plan["inside_error_bound"]) or
                            np.any(np.abs(delta[~mask]) > plan["outside_error_bound"]) or
                            np.any(result[~mask] != compensated[~mask])
                        ):
                            raise ValueError("Observed error exceeds its bound or the disturbance leaks outside ROI")
                    row.update(status="completed", global_pass=1000*errors["processed"][0] <= PEAK_SQUARED*total,
                               roi_decreased=errors["processed"][1] > errors["baseline"][1],
                               roi_decreased_vs_compensated=errors["processed"][1] > errors["compensated"][1])
                except (ValueError, OSError, subprocess.CalledProcessError) as error:
                    message = (error.stderr or str(error)).strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
                    failures.append(dict(image=path.name, application=app, error=message))
            print(f"Verified {index+1}/{len(inputs)}: {path.name}", flush=True)
    fields = list(dict.fromkeys(key for row in rows for key in row))
    with (output/"images.csv").open("x", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    summary = {
        app: dict(images=len(inputs), completed=sum(r["status"] == "completed" for r in rows if r["application"] == app),
                  global_pass=sum(r["global_pass"] for r in rows if r["application"] == app),
                  roi_decreased=sum(r["roi_decreased"] for r in rows if r["application"] == app),
                  joint_pass=sum(r["global_pass"] and r["roi_decreased"]
                                 for r in rows if r["application"] == app),
                  roi_decreased_vs_compensated=sum(r["roi_decreased_vs_compensated"] for r in rows if r["application"] == app))
        for app in applications
    }
    write_json(output/"summary.json", dict(config_sha256=sha256(args.config),
                                          binary_sha256=sha256(args.binary), applications=summary,
                                          failures=failures))
    print(json.dumps(summary, indent=2))
    return 1 if failures or any(not r["global_pass"] for r in rows) else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prepare = sub.add_parser("freeze")
    prepare.add_argument("--roi-run", type=Path, required=True)
    prepare.add_argument("--threshold", type=int, default=85)
    prepare.add_argument("--min-bit", type=int, choices=range(8), default=4)
    prepare.add_argument("--output", type=Path, required=True)
    verify = sub.add_parser("verify")
    verify.add_argument("--config", type=Path, required=True)
    verify.add_argument("--binary", type=Path, default=ROOT/"build/fixed_roi_verify")
    verify.add_argument("--images", type=Path, required=True)
    verify.add_argument("--start", type=int, default=1)
    verify.add_argument("--count", type=int, required=True)
    verify.add_argument("--apps", nargs="+", choices=APPLICATIONS)
    verify.add_argument("--save-examples", type=int, default=3)
    verify.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "freeze":
            freeze(args)
            return 0
        return evaluate(args)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
