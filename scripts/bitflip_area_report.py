"""Strict validation of flat Yosys stat/netlist JSON and local area summaries.

Stat schema: YosysHQ/yosys passes/cmds/stat.cc, log_data_json, without
-hierarchy (numeric area, num_cells and num_cells_by_type). No tool execution.
"""
from collections import Counter
import csv
import math
from pathlib import Path


def _positive(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(f"{label} must be finite and positive")
    return float(value)


def _count(value):
    if type(value) is not int or value < 0:
        raise ValueError("Invalid cell count in stat JSON")
    return value


def validate_result(stat, mapped, top, library, expected_config_bits):
    """Cross-check tool area/counts against every mapped standard-cell instance."""
    try:
        modules = mapped["modules"]
        cells = modules[top]["cells"]
        if modules[top].get("memories") or modules[top].get("processes"):
            raise ValueError("Unmapped memory or process remains in mapped top")
        stats = stat["modules"]
        top_stat = stats.get("\\" + top, stats.get(top))
        if top_stat is None:
            raise ValueError("Missing top module in stat JSON")
        areas = library["cell_areas"]
        verified = set(library["lef_verified_cells"])
        sequential = set(library["sequential_cells"])
        counts = Counter()
        seq_bits = 0
        sequential_area = 0.0
        total = 0.0
        for cell in cells.values():
            name = cell["type"]
            if "$" in name or name not in areas or name not in verified:
                raise ValueError(f"Internal, unmapped or LEF-unverified cell: {name}")
            counts[name] += 1
            total += _positive(areas[name], f"Library area for {name}")
            if name in sequential:
                sequential_area += areas[name]
                # Pinned Nangate DFFs expose one stored bit through Q and QN.
                # Reject bus/multi-bit outputs; count complementary Q/QN once.
                connections = cell.get("connections", {})
                outputs = cell.get("port_directions", {})
                qports = [p for p in ("Q", "QN") if p in connections]
                if not qports or any(len(connections[p]) != 1 for p in qports):
                    raise ValueError("Unsupported multi-bit or unidentified sequential cell")
                if any(len(connections[p]) != 1 for p, d in outputs.items() if d == "output"):
                    raise ValueError("Unsupported multi-bit sequential output")
                seq_bits += 1
        if seq_bits != expected_config_bits:
            raise ValueError(f"Retained sequential bits {seq_bits} != expected {expected_config_bits}")
        if not cells:
            raise ValueError("Empty mapped top")
        for entry in [top_stat] + ([stat["design"]] if "design" in stat else []):
            reported = entry["num_cells_by_type"]
            if not isinstance(reported, dict) or any(_count(n) == 0 for n in reported.values()):
                raise ValueError("Invalid per-type cell counts")
            if dict(counts) != reported or _count(entry["num_cells"]) != len(cells):
                raise ValueError("Stat/netlist cell counts disagree")
            if entry.get("unknown_cell_area"):
                raise ValueError("Stat contains unknown cell area")
            for key in ("num_memories", "num_memory_bits", "num_processes", "num_submodules"):
                if key in entry and _count(entry[key]) != 0:
                    raise ValueError(f"Unmapped or non-flat design: {key}")
            area = _positive(entry.get("area"), "Stat area")
            # Yosys serializes stat areas with six decimal digits.
            if not math.isclose(area, total, rel_tol=1e-8, abs_tol=1e-5):
                raise ValueError("Stat area disagrees with sum of verified Liberty cell areas")
        # Extra library blackboxes are harmless; nonblackbox design modules are not.
        for name, mod in modules.items():
            if name == top:
                continue
            blackbox = mod.get("attributes", {}).get("blackbox", 0)
            if name not in areas or not blackbox or str(blackbox).strip("0") == "":
                raise ValueError(f"Unexpected unflattened module: {name}")
        return {"area_um2": area, "verified_area_um2": total,
                "num_cells": len(cells), "cells_by_type": dict(counts), "sequential_bits": seq_bits,
                "sequential_area_um2": sequential_area, "combinational_area_um2": total-sequential_area}
    except (KeyError, TypeError, AttributeError) as exc:
        raise ValueError(f"Malformed or missing mapped/stat report: {exc}") from exc


def write_summary(output: Path, jobs: list):
    """Write only after every job passed validation. Never add component areas."""
    rows = []
    for application in dict.fromkeys(job["application"] for job in jobs):
        areas = {j["kind"]: j["measurement"]["area_um2"] for j in jobs if j["application"] == application}
        delta = areas["host_attacked"] - areas["host_baseline"]
        complete=next(j['measurement'] for j in jobs if j['application']==application and j['kind']=='attack_complete')
        rows.append({"application": application, "scope": "selected_unit_bank_not_full_application",
                     **{kind + "_um2": value for kind, value in areas.items()},
                     "complete_combinational_um2":complete['combinational_area_um2'],
                     "complete_sequential_um2":complete['sequential_area_um2'],
                     "integrated_delta_um2": delta,
                     "selected_bank_overhead_percent": 100 * delta / areas["host_baseline"]})
    with (output / "summary.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    text = ["# Fixed-bitflip mapped cell area", "",
            "Scope: selected arithmetic-unit bank; not the complete image-processing application.",
            "Area unit: um^2, checked against Liberty and matching LEF geometry. This is mapped cell area, not die area.",
            "Independently synthesized components are not additive. Integrated delta = host_attacked - host_baseline.",
            "The percentage uses only host_baseline bank area. No whole-application percentage, timing or power is claimed.", "",
            "The complete module is also split by mapped combinational/sequential cell area; config_storage includes load logic and is a separate synthesis.",
            "| Application | Attack comb | Config storage | Attack complete | Host baseline | Host attacked | Complete comb | Complete seq | Integrated delta | Bank overhead (%) |",
            "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for row in rows:
        keys = [k for k in row if k not in ("application", "scope")]
        text.append("| " + row["application"] + " | " + " | ".join(f"{row[k]:.6f}" for k in keys) + " |")
    (output / "summary.md").write_text("\n".join(text) + "\n", encoding="utf-8")
