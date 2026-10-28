#!/usr/bin/env python3
"""Manual Yosys/Nangate45 area runner. Import and --dry-run never launch tools."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from bitflip_area_report import validate_result, write_summary

KINDS = ("attack_comb", "config_storage", "attack_complete", "host_baseline", "host_attacked")
SCOPE = "selected_unit_bank_not_full_application"
LEGACY_SCOPE = "single_arithmetic_kernel_not_full_image_system"


def generate_design(output, config_path=None):
    from bitflip_area_design import generate_design as generate
    return generate(output, config_path)


def generate_legacy_design(output, config_path=None):
    from bitflip_legacy_design import generate_design as generate
    return generate(output, config_path)


def load_library_manifest(path):
    from area_library import load_library_manifest as load
    return load(path)


def _sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _verify_sources(expected):
    for path, digest in expected.items():
        if not Path(path).is_file() or _sha(path) != digest:
            raise ValueError(f"Source changed during area run: {path}")


def _validate_config_counts(app):
    bits = app["config_bits"]
    if type(bits) is not int or bits <= 0 or bits % 2:
        raise ValueError("Configuration bit count must be a positive even integer")
    monitor = app.get("monitor_bits", bits // 2)
    if type(monitor) is not int or monitor <= 0 or bits != 2 * monitor:
        raise ValueError("Configuration bits disagree with monitor bits")
    nodes = app.get("nodes")
    node_count = len(nodes) if isinstance(nodes, list) else nodes
    if type(node_count) is not int or node_count <= 0 or not 12 * node_count <= monitor <= 13 * node_count:
        raise ValueError("Monitor bits disagree with selected node count")
    if "node_profiles" in app:
        profiles = app["node_profiles"]
        if len(profiles) != node_count or any(p.get("monitor_width") not in (12, 13) for p in profiles):
            raise ValueError("Invalid monitor widths in node profiles")
        if sum(p["monitor_width"] for p in profiles) != monitor:
            raise ValueError("Monitor bits disagree with node profiles")


def _save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def _overlap(a, b):
    return a == b or a in b.parents or b in a.parents


def _quote(path):
    value = Path(path).as_posix()
    if any(c in value for c in ('\n', '\r', '"', ';')):
        raise ValueError("Unsupported character in Yosys script path")
    return '"' + value + '"'


def _script(top, rtl_files, liberty, result, legacy_kernel=False):
    # tee in Yosys 0.33 passes its output token directly to fopen without
    # unquoting it. The runner's cwd is the output directory; this generated
    # relative token has no whitespace even when the output directory does.
    report_path = "results/" + top + "/stat.json"
    recipe = [
        "# Fixed area recipe; no clock target, physical implementation or power estimate.",
        *([] if legacy_kernel else ["read_liberty -lib " + _quote(liberty)]),
        "read_verilog -sv " + " ".join(_quote(p) for p in rtl_files),
        *([] if legacy_kernel else ["hierarchy -check -top " + top]),
        ("synth -top " + top + " -flatten -noabc") if legacy_kernel else ("synth -flatten -noabc -top " + top),
        *([] if legacy_kernel else ["dffunmap"]),
        "dfflibmap -liberty " + _quote(liberty),
        "abc -liberty " + _quote(liberty),
        "clean",
        *(["read_liberty -lib " + _quote(liberty)] if legacy_kernel else []),
        "check -assert",
        "select -assert-none t:$*",
        "tee -o " + report_path + " stat -json -liberty " + _quote(liberty) + " -top " + top,
        "write_verilog -noattr " + _quote(result / "mapped.v"),
        "write_json " + _quote(result / "mapped.json"), ""]
    return "\n".join(recipe)


def run_area(library_path, output, config_path=None, dry_run=False, legacy_kernel=False):
    library_path, output = Path(library_path).resolve(), Path(output).resolve()
    config_path = Path(config_path).resolve() if config_path else None
    repo = Path(__file__).resolve().parents[1]
    protected = [library_path, *([config_path] if config_path else []),
                 repo / "scripts", repo / "hardware", repo / "sources", repo / "tests"]
    if output.exists() or any(_overlap(output, p) for p in protected):
        raise ValueError("Output must be new and must not overlap inputs or source directories")
    library = load_library_manifest(library_path)
    scope = LEGACY_SCOPE if legacy_kernel else SCOPE
    if library.get("area_unit") != "um^2" or not library.get("cell_areas") or not library.get("lef_verified_cells"):
        raise ValueError("Unsupported library or unverified area units")
    liberty = Path(library["liberty_path"]).resolve()
    library_files = [liberty] + [Path(library[key]).resolve() for key in ("lef_path", "license_path") if library.get(key)]
    if any(_overlap(output, path) for path in library_files):
        raise ValueError("Output overlaps a verified library input")
    output.mkdir(parents=True)
    manifest = {"schema": "bitflip_area_run_v1", "state": "preparing", "dry_run": dry_run,
                "scope": scope, "recipe": "legacy_runtime" if legacy_kernel else "selected_bank_v1",
                "area_unit": "um^2", "started_utc": datetime.now(timezone.utc).isoformat(),
                "library": library, "tool_version": None, "jobs": [], "source_sha256": {}}
    manifest_path = output / "run_manifest.json"
    _save(manifest_path, manifest)
    try:
        design_dir = output / "design"
        design = (generate_legacy_design if legacy_kernel else generate_design)(design_dir, config_path)
        if design.get("schema") != "bitflip_area_design_v1" or design.get("scope") != scope:
            raise ValueError("Unsupported design manifest")
        if legacy_kernel and design.get('liberty_sha256') != _sha(liberty):
            raise ValueError('Library differs from historical Liberty bytes')
        rtl_files = []
        for value in design["rtl_files"]:
            path = (design_dir / value).resolve()
            if Path(value).is_absolute() or design_dir.resolve() not in path.parents or not path.is_file():
                raise ValueError("RTL path escapes generated design or is missing")
            rtl_files.append(path)
        if not rtl_files:
            raise ValueError("No generated RTL files")
        apps = design["applications"]
        if len(apps) != 4 or {a["application"] for a in apps} != {"sharpen", "conv", "sobel", "dct"}:
            raise ValueError("Expected exactly the four supported applications")
        (output / "scripts").mkdir()
        (output / "results").mkdir()
        for app in apps:
            if set(app["tops"]) != set(KINDS):
                raise ValueError("Expected all five synthesis tops")
            _validate_config_counts(app)
            for kind in KINDS:
                top = app["tops"][kind]
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", top):
                    raise ValueError("Unsafe or unsupported top identifier")
                result = output / "results" / top
                result.mkdir()  # Duplicate tops are rejected here.
                script = output / "scripts" / (top + ".ys")
                script.write_text(_script(top, rtl_files, liberty, result, legacy_kernel), encoding="utf-8")
                manifest["jobs"].append({"application": app["application"], "kind": kind, "top": top,
                    "expected_config_bits": app["config_bits"] if kind in ("config_storage", "attack_complete", "host_attacked") else 0,
                    "script": str(script.relative_to(output)), "log": str((result / "yosys.log").relative_to(output)),
                    "state": "planned"})
        sources = [library_path, *library_files, design_dir / "design_manifest.json", *rtl_files,
                   Path(__file__).resolve(), Path(__file__).with_name("bitflip_area_report.py"),
                   *list((output / "scripts").glob("*.ys"))]
        if design.get('configuration'):
            sources.append(Path(design['configuration']).resolve())
        if design.get('config_snapshot'):
            snapshot=(design_dir/design['config_snapshot']).resolve()
            if design_dir.resolve() not in snapshot.parents:
                raise ValueError('Configuration snapshot escapes design directory')
            sources.append(snapshot)
        sources.extend(Path(p).resolve() for p in design.get('source_dependencies',[]))
        for dependency in ("area_library.py", "bitflip_area_design.py", *(['bitflip_legacy_design.py'] if legacy_kernel else [])):
            path = Path(__file__).with_name(dependency)
            if path.is_file():
                sources.append(path)
        if config_path:
            sources.append(config_path)
        manifest["source_sha256"] = {str(p): _sha(p) for p in sources}
        if dry_run:
            manifest["state"] = "dry_run"
            _save(manifest_path, manifest)
            return manifest
        manifest["state"] = "running"
        _save(manifest_path, manifest)
        with (output / "yosys_version.log").open("w", encoding="utf-8") as log:
            version = subprocess.run(["yosys", "-V"], cwd=output, stdout=log, stderr=subprocess.STDOUT, text=True, check=False)
        if version.returncode:
            raise RuntimeError("Yosys version query failed; see yosys_version.log")
        manifest["tool_version"] = (output / "yosys_version.log").read_text(encoding="utf-8").strip()
        if not manifest["tool_version"]:
            raise RuntimeError("Empty Yosys version output")
        for job in manifest["jobs"]:
            _verify_sources(manifest["source_sha256"])
            job["state"] = "running"
            _save(manifest_path, manifest)
            with (output / job["log"]).open("w", encoding="utf-8") as log:
                command = ["yosys", "-s", str(output / job["script"])]
                job["command"] = command
                process = subprocess.run(command, cwd=output, stdout=log, stderr=subprocess.STDOUT, text=True, check=False)
            job["returncode"] = process.returncode
            if process.returncode:
                raise RuntimeError(f"Yosys failed for {job['top']}; see {job['log']}")
            result = output / "results" / job["top"]
            for name in ("stat.json", "mapped.json", "mapped.v"):
                if not (result / name).is_file() or not (result / name).stat().st_size:
                    raise ValueError(f"Missing or empty output {job['top']}/{name}")
            options={'allow_pruned_config':True} if legacy_kernel and job['kind']=='host_attacked' else {}
            job["measurement"] = validate_result(json.loads((result / "stat.json").read_text()),
                json.loads((result / "mapped.json").read_text()), job["top"], library, job["expected_config_bits"],**options)
            job["output_sha256"] = {name: _sha(result / name) for name in ("stat.json", "mapped.json", "mapped.v", "yosys.log")}
            job["state"] = "complete"
            _save(manifest_path, manifest)
        _verify_sources(manifest["source_sha256"])
        write_summary(output, manifest["jobs"], design if legacy_kernel else None)
        manifest["state"] = "complete"
        manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
        _save(manifest_path, manifest)
        return manifest
    except BaseException as exc:
        manifest["state"] = "failed"
        manifest["error"] = f"{type(exc).__name__}: {exc}"
        for job in manifest["jobs"]:
            if job["state"] == "running":
                job["state"] = "failed"
        _save(manifest_path, manifest)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--legacy-kernel",action='store_true',help='Use previous full arithmetic cores and mapping recipe with the identical Liberty; preserve current hardware configuration')
    parser.add_argument("--dry-run", action="store_true", help="Generate RTL and plans without invoking Yosys, including version queries")
    args = parser.parse_args(argv)
    try:
        result = run_area(args.library, args.output, args.config, args.dry_run,args.legacy_kernel)
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"Area run failed: {exc}", file=sys.stderr)
        return 1
    print(f"Area run state: {result['state']}; outputs: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
