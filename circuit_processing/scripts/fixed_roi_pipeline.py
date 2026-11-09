#!/usr/bin/env python3
"""Freeze a configuration from arithmetic bounds, then verify it on images."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile

import legacy_circuit_profiles as profiles
import legacy_planning
import circuit_ir
import unit_policy
import circuit_sources
from excel_report import export_psnr_workbook, export_results_directory
from comparison_images import save_comparison


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


def within_budget(area, total, inside_error, outside_error):
    return 1000 * (area*inside_error**2 + (total-area)*outside_error**2) <= PEAK_SQUARED*total



def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_identity():
    paths = [Path(__file__), ROOT/"app/fixed_roi_verify.cpp",
             ROOT/"include/core/output_bitflip.hpp",
             ROOT/"src/approximate/evoapprox_adapter.cpp",
             ROOT/"third_party/evoapproxlib/add12se_5RP.c"]
    paths.extend(ROOT/f"src/applications/{app}.cpp" for app in (*APPLICATIONS, "dct8_fixed_graph"))
    paths.extend((ROOT/"scripts/legacy_circuit_profiles.py", ROOT/"configs/unit_error_bounds.json"))
    paths.extend(ROOT/f"scripts/{name}.py" for name in ("legacy_planning", "circuit_sources", "c_kernel_encoder", "circuit_ir", "unit_policy", "circuit_energy"))
    paths.extend(sorted((ROOT/"app").glob("*.hpp")))
    paths.extend(path for path in sorted((ROOT/"include").rglob("*.hpp")) if path not in paths)
    paths.extend(path for path in sorted((ROOT/"src").rglob("*.cpp")) if path not in paths)
    paths.extend(path for path in sorted((ROOT/"third_party/evoapproxlib").glob("*.c")) if path not in paths)
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


def legacy_profile_graph(profile):
    if profile["application"] == "graph":
        return profile["graph"]
    if profile["application"] != "dct":
        return profiles.graph_from_tuples(legacy_planning.spatial_graph(profile["application"]))
    return None


def unit_bounds():
    document = read_json(ROOT/"configs/unit_error_bounds.json")
    for relative, digest in document["sources"].items():
        if hashlib.sha256((ROOT/relative).read_text(encoding="utf-8").encode()).hexdigest() != digest:
            raise ValueError(f"Re-audit signed adder error bounds after changing {relative}")
    return document["units"]


def select_profiles(args):
    if args.circuit:
        selected = [circuit_sources.load_profile(path) for path in args.circuit]
    else:
        selected = [circuit_sources.builtin_source(profiles.default_profile(app))
                    for app in (args.apps or APPLICATIONS)]
    if len({profile["name"] for profile in selected}) != len(selected):
        raise ValueError("Circuit profile names must be unique")
    return {profile["name"]: profile for profile in selected}


def profile_digest(selected):
    canonical = json.dumps(selected, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def compatible_sources(frozen_sources, current_sources):
    snapshots = read_json(ROOT/"configs/legacy_schema2_sources.json")
    return frozen_sources in snapshots


def legacy_plan_profiles(selected, area, total, min_bit, max_subsets):
    bounds = unit_bounds()
    return {name: profiles.plan(profile, legacy_profile_graph(profile), bounds, area, total,
                               min_bit, within_budget, max_subsets)
            for name, profile in selected.items()}


def plan_profiles(selected, area, total, min_bit, max_evaluations, max_candidates, *,
                  policy=None, grid=None, box=None):
    bounds = unit_bounds()
    result = {}
    for name, profile in selected.items():
        if policy is None:
            choice = circuit_ir.plan(profile, bounds, area, total, min_bit,
                                     max_evaluations, max_candidates)
        else:
            choice = unit_policy.plan(profile, bounds, policy["costs"], grid, box,
                                      min_bit, max_evaluations, max_candidates,
                                      policy["min_strength_ratio"])
        if choice['mode'] == 'no_certified_configuration':
            raise ValueError(f"{name}: {choice['reason']}")
        if choice['mode'] == 'ir_internal':
            operation = next(item for item in profile['ir']['operations'] if item['id'] == choice['node'])
            choice['unit_node'] = operation['unit_node']
            choice['source_coord'] = operation.get('source_coord')
            choice['call_stack'] = operation.get('call_stack', [])
        result[name] = choice
    return result


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
    selected = select_profiles(args)
    policy = (dict(costs=read_json(args.unit_costs), min_strength_ratio=args.min_strength_ratio)
              if args.unit_costs else None)
    config = dict(schema=4 if policy else 3, grid=grid, bbox_half_open=bounds, frequency_percent=args.threshold,
                  roi_images=protocol["count"], roi_run_sha256=sha256(args.roi_run/"rectangles.json"),
                  min_bit=args.min_bit, max_evaluations=args.max_evaluations, max_candidates=args.max_candidates, sources=checked_sources(),
                  input_circuits=selected, input_circuits_sha256=profile_digest(selected),
                  circuits=plan_profiles(selected, area, grid*grid, args.min_bit, args.max_evaluations,
                                         args.max_candidates, policy=policy, grid=grid, box=bounds))
    if policy:
        config["unit_policy"] = policy
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_json(args.output, config)
    for app, choice in config["circuits"].items():
        print(f"{app}: {choice['mode']}, node {choice['node']}, bit {choice['bit']}, exact {choice['exact_nodes']}")
        if policy:
            mse = choice['global_mse_bound']
            psnr = 10*math.log10(PEAK_SQUARED/mse) if mse else math.inf
            print(f"  approximate {choice['unit_replacements']}, "
                  f"estimated units {choice['unit_area_um2']:.2f} um^2, "
                  f"PSNR bound {psnr:.2f} dB")


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
    current_sources = checked_sources()
    schema = config.get("schema")
    profiles.integer(config["min_bit"], "min_bit", 0, 7)
    grid, bounds = config["grid"], config["bbox_half_open"]
    if type(grid) is not int or grid < 2 or not isinstance(bounds, list) or len(bounds) != 4:
        raise ValueError("Invalid frozen grid or rectangle")
    if any(type(value) is not int for value in bounds):
        raise ValueError("Rectangle coordinates must be integers")
    l, t, r, b = bounds
    if not 0 <= l < r <= grid or not 0 <= t < b <= grid:
        raise ValueError("Invalid frozen rectangle")
    if schema == 1:
        legacy_sources = read_json(ROOT/"configs/legacy_frozen_sources.json")
        if config["sources"] != legacy_sources:
            raise ValueError("Legacy configuration belongs to an unsupported source revision")
        selected = {app: profiles.default_profile(app) for app in APPLICATIONS}
        expected = {app: legacy_planning.plan_circuit(app, (r-l)*(b-t), grid**2, config["min_bit"]) for app in APPLICATIONS}
    elif schema == 2:
        profiles.integer(config["max_subsets"], "max_subsets", 1, 1000000)
        if not compatible_sources(config["sources"], current_sources):
            raise ValueError("Frozen configuration belongs to a different source revision")
        selected = config["input_circuits"]
        if not isinstance(selected, dict) or not selected:
            raise ValueError("Frozen configuration needs input circuits")
        if profile_digest(selected) != config["input_circuits_sha256"]:
            raise ValueError("Frozen input circuit definitions changed")
        for name, profile in selected.items():
            profiles.validate_profile(profile)
            if name != profile["name"]:
                raise ValueError("Circuit profile key and name differ")
        expected = legacy_plan_profiles(selected, (r-l)*(b-t), grid**2, config["min_bit"], config["max_subsets"])
    elif schema in (3, 4):
        legacy_ir_sources = schema == 3 and config["sources"] == read_json(ROOT/"configs/legacy_schema3_sources.json")
        if config["sources"] != current_sources and not legacy_ir_sources:
            raise ValueError("Frozen IR configuration belongs to a different source revision")
        selected = config["input_circuits"]
        if not isinstance(selected, dict) or not selected or profile_digest(selected) != config["input_circuits_sha256"]:
            raise ValueError("Frozen input circuits changed")
        for name, profile in selected.items():
            circuit_ir.validate_profile(profile)
            if name != profile["name"]:
                raise ValueError("Circuit name differs from its key")
        profiles.integer(config["max_evaluations"], "max-evaluations", 4, 100000)
        profiles.integer(config["max_candidates"], "max-candidates", 1, 10000)
        expected = plan_profiles(selected, (r-l)*(b-t), grid**2, config["min_bit"],
                                 config["max_evaluations"], config["max_candidates"],
                                 policy=config["unit_policy"] if schema == 4 else None, grid=grid, box=bounds)
    else:
        raise ValueError("Unsupported frozen configuration schema")
    if config["circuits"] != expected:
        raise ValueError("Frozen circuit configuration differs from the code-derived result")
    applications = args.apps or list(selected)
    if len(set(applications)) != len(applications) or any(app not in selected for app in applications):
        raise ValueError("--apps must name unique circuits present in the frozen configuration")
    image_dir = args.images.expanduser().resolve()
    output = args.output.resolve()
    if output.is_relative_to(image_dir) or image_dir.is_relative_to(output):
        raise ValueError("Output and input image directories must be separate")
    if not args.binary.is_file():
        raise ValueError(f"Missing compiled verifier: {args.binary}")
    if schema in (3, 4):
        capability = subprocess.run([str(args.binary.resolve()), '--capabilities'], capture_output=True, text=True)
        try:
            features = json.loads(capability.stdout)
            supported = (capability.returncode == 0 and features.get('circuit_ir') == 1
                         and (schema == 3 or features.get('approximate_replacements') == 1))
        except (ValueError, AttributeError):
            supported = False
        if not supported:
            raise ValueError('Rebuild the native verifier for circuit IR: cmake --build circuit_processing/build -j 4')
    if args.start < 1 or args.count < 1 or (args.save_examples is not None and args.save_examples < 0):
        raise ValueError("Invalid image range or example count")
    inputs = [image_dir/f"{index}.jpg" for index in range(args.start, args.start+args.count)]
    for path in inputs:
        if not path.is_file():
            raise ValueError(f"Missing input image: {path}")
    output.mkdir(parents=True, exist_ok=False)
    comparisons = output/"comparisons"
    comparisons.mkdir()
    rows = []
    failures = []
    with tempfile.TemporaryDirectory(dir=output) as scratch:
        scratch = Path(scratch)
        profile_paths = {}
        if schema in (2, 3, 4):
            for name in applications:
                profile_paths[name] = scratch/f"{name}.json"
                write_json(profile_paths[name], selected[name])
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
                    fits = (unit_policy.configuration_mse(selected[app], plan, width, height, box) <= PEAK_SQUARED/1000
                            if schema == 4 else within_budget(area, total, plan["inside_error_bound"], plan["outside_error_bound"]))
                    if area == 0 or not fits:
                        raise ValueError("Projected rectangle is empty or exceeds the frozen error budget")
                    destination = scratch
                    extra = ["--circuit", str(profile_paths[app])] if schema in (2, 3, 4) else []
                    replacements = [str(g) for g in plan["exact_nodes"]]
                    replacements.extend(f"{entry['node']}={entry['unit']}" for entry in plan.get("unit_replacements", []))
                    subprocess.run([str(args.binary.resolve()), selected[app]["application"], str(path), str(destination),
                                    *map(str, box), str(plan["node"]), str(plan["bit"]),
                                    *replacements, *extra], check=True, capture_output=True, text=True)
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
                        if name == "processed" and schema == 4 and errors[name][0]/total > unit_policy.configuration_mse(
                            selected[app], plan, width, height, box
                        ):
                            raise ValueError("Observed MSE exceeds the frozen frame certificate")
                    save_comparison(destination, comparisons/f"{app}_{int(path.stem):04d}.png", row)
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
    report = export_psnr_workbook(rows, output/"psnr_results.xlsx")
    print(json.dumps(summary, indent=2))
    print(f"Excel report: {report}")
    print(f"Comparison images: {comparisons}")
    return 1 if failures or any(not r["global_pass"] for r in rows) else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prepare = sub.add_parser("freeze")
    prepare.add_argument("--roi-run", type=Path, required=True)
    prepare.add_argument("--threshold", type=int, default=85)
    prepare.add_argument("--min-bit", type=int, choices=range(8), default=4)
    prepare.add_argument("--output", type=Path, required=True)
    prepare.add_argument("--circuit", type=Path, action="append", default=[],
                         help="Input circuit JSON; repeat to compare independent baselines")
    prepare.add_argument("--apps", nargs="+", choices=APPLICATIONS,
                         help="Select built-in default circuits when --circuit is omitted")
    prepare.add_argument("--max-subsets", type=int, default=10000,
                         help="Legacy option accepted only for old frozen configurations")
    prepare.add_argument("--max-evaluations", type=int, default=512)
    prepare.add_argument("--max-candidates", type=int, default=128)
    prepare.add_argument("--unit-costs", type=Path, help="Use area-aware unit selection with this cost table")
    prepare.add_argument("--min-strength-ratio", type=float, default=0.5,
                         help="Minimum structural flip score relative to the strongest certified candidate")
    encode = sub.add_parser("encode-c", help="Encode a supported C kernel into the shared circuit IR")
    encode.add_argument("--interface", type=Path, required=True)
    encode.add_argument("--output", type=Path, required=True)
    listing = sub.add_parser("list-circuits", help="Show built-in node counts and supported units")
    report = sub.add_parser("export-excel", help="Export an existing verify directory without rerunning images")
    report.add_argument("--results", type=Path, required=True)
    verify = sub.add_parser("verify")
    verify.add_argument("--config", type=Path, required=True)
    verify.add_argument("--binary", type=Path, default=ROOT/"build/fixed_roi_verify")
    verify.add_argument("--images", type=Path, required=True)
    verify.add_argument("--start", type=int, default=1)
    verify.add_argument("--count", type=int, required=True)
    verify.add_argument("--apps", nargs="+", help="Names present in the frozen input circuits")
    verify.add_argument("--save-examples", type=int,
                        help="Legacy option accepted for compatibility; all comparison images are saved")
    verify.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "encode-c":
            encoded = circuit_sources.load_profile(args.interface)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            write_json(args.output, encoded)
            print(f"Encoded {len(encoded['ir']['operations'])} operations: {args.output}")
            return 0
        if args.command == "export-excel":
            report = export_results_directory(args.results.expanduser().resolve())
            print(f"Excel report: {report}")
            return 0
        if args.command == "list-circuits":
            print(json.dumps(dict(applications=profiles.NODE_COUNTS, units=profiles.UNITS), indent=2))
            return 0
        if args.command == "freeze":
            if args.circuit and args.apps:
                raise ValueError("Choose --circuit profiles or --apps defaults")
            profiles.integer(args.max_evaluations, "max-evaluations", 4, 100000)
            profiles.integer(args.max_candidates, "max-candidates", 1, 10000)
            freeze(args)
            return 0
        return evaluate(args)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
