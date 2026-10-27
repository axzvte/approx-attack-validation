"""UNRUN: user-run contracts for local discovery and continuous fixed-H CLI.

Usage: python tests/test_circuit_bitflip_continuation_cli.py JOINT_EXE FIXED_EXE
Successful invocations include each adapter's original-application replay checks.
"""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile


JOINT, FIXED = (str(Path(value).resolve()) for value in sys.argv[1:3])
FIELDS = ("seed_id", "node_id", "monitor", "bit", "lower", "upper")


def rows(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def write_csv(path, fields, values):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(fields)
        writer.writerows(values)


def invoke(executable, application, data, output, *flags, ok=True):
    process = subprocess.run([executable, application, str(data), str(output), *map(str, flags)],
                             capture_output=True, text=True)
    assert (process.returncode == 0) == ok, (process.returncode, process.stdout, process.stderr)
    return process


def run_values(output):
    return dict(line.split("=", 1) for line in (output / "RUN.txt").read_text().splitlines() if "=" in line)


def metrics(output):
    return [(r["candidate_id"], r["status"], r["global_mse"], r["roi_mse"], r["non_roi_mse"])
            for r in rows(output / "summary.csv")]


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    data = root / "data"
    (data / "stage1/input").mkdir(parents=True)
    (data / "mask").mkdir()
    (data / "stage1/input/image_01.jpg").write_bytes(b"P5\n16 16\n255\n" + bytes(
        112 + (3 * y + 5 * x + x * y) % 32 for y in range(16) for x in range(16)))
    (data / "mask/roi_mask.jpg").write_bytes(b"P5\n16 16\n255\n" + bytes(
        255 if 4 <= y < 12 and 4 <= x < 12 else (1 if x % 2 else 127)
        for y in range(16) for x in range(16)))
    for app, native in (("conv", 8), ("sobel", 11), ("dct", 32)):
        seed = root / f"{app}_structures.csv"
        dormant = [("current_train", n, "input1", 0, -2048, -2048) for n in (0, native - 1)]
        write_csv(seed, FIELDS, dormant)
        common = ["--beam", 1, "--refinement", 0, "--restarts", 1, "--boundary-budget", 2,
                  "--max-contexts", 6, "--max-full-evals", 64, "--dct-full-candidates", 2,
                  "--bits", "0", "--top-per-count", 1, "--global-psnr", 0]
        seeded_flags = [*common, "--structure-seeds", seed, "--structure-radius", 1,
                        "--exploration-contexts", 2]
        output = root / f"{app}_local"
        invoke(JOINT, app, data, output, *seeded_flags)
        stats = rows(output / "search_stats.csv")[0]
        assert int(stats["accepted_structure_seeds"]) == 1
        assert int(stats["rejected_structure_seeds"]) == 0
        local, global_ = int(stats["local_contexts"]), int(stats["global_contexts"])
        assert local > 0 and global_ <= 2 and local + global_ <= 6
        assert int(stats["contexts_built"]) == local + global_
        assert int(stats["local_candidates_filtered"]) >= 0
        run = run_values(output)
        assert run["structure_radius"] == "1" and run["exploration_contexts"] == "2"
        assert Path(run["structure_seeds"]) == seed.resolve()
        for field in ("accepted_structure_seeds", "rejected_structure_seeds", "local_contexts",
                      "global_contexts", "local_candidates_filtered"):
            assert run[field] == stats[field]
        manifest = rows(output / "images.csv")
        numeric = [row for row in rows(output / "summary.csv") if row["status"] == "numeric_eligible"]
        assert len(manifest) == (3 if numeric else 0)
        if manifest:
            assert {r["image_kind"] for r in manifest} == {"output", "roi_comparison", "background_comparison"}
        none = root / f"{app}_local_none"
        invoke(JOINT, app, data, none, *seeded_flags, "--image-mode", "none")
        assert metrics(output) == metrics(none)
        assert rows(output / "configurations.csv") == rows(none / "configurations.csv")
        assert not list(none.glob("*.png"))
        # New defaults preserve global discovery when no seed file is supplied.
        legacy = root / f"{app}_legacy"
        defaults = root / f"{app}_defaults"
        invoke(JOINT, app, data, legacy, *common, "--image-mode", "none")
        invoke(JOINT, app, data, defaults, *common, "--structure-radius", 1,
               "--exploration-contexts", 0, "--image-mode", "none")
        assert metrics(legacy) == metrics(defaults)
        unseeded = rows(legacy / "search_stats.csv")[0]
        assert unseeded["accepted_structure_seeds"] == unseeded["local_contexts"] == "0"
        assert int(unseeded["global_contexts"]) == int(unseeded["contexts_built"]) <= 6
        # An empty seed bank is a truthful bounded global fallback.
        empty = root / f"{app}_empty.csv"
        write_csv(empty, FIELDS, [])
        fallback = root / f"{app}_fallback"
        invoke(JOINT, app, data, fallback, *common, "--structure-seeds", empty,
               "--exploration-contexts", 2, "--image-mode", "none")
        fallback_stats = rows(fallback / "search_stats.csv")[0]
        assert fallback_stats["local_contexts"] == fallback_stats["accepted_structure_seeds"] == "0"
        assert int(fallback_stats["global_contexts"]) == int(fallback_stats["contexts_built"]) <= 6
        invalid = [dormant[:1], dormant + dormant[:1],
                   [("", *row[1:]) for row in dormant],
                   [("s", -1, "input1", 0, 0, 0), dormant[1]],
                   [("s", native, "input1", 0, 0, 0), dormant[1]],
                   [("current_train", 0, "unknown", 0, 0, 0), dormant[1]],
                   [("current_train", 0, "input1", 12, 0, 0), dormant[1]],
                   [("current_train", 0, "input1", 0, -2049, 0), dormant[1]],
                   [("current_train", 0, "input2", 0, 0, 2048), dormant[1]],
                   [("current_train", 0, "baseline_output", 11, 0, 4096), dormant[1]],
                   [("current_train", 0, "input1", 0, 1, 0), dormant[1]]]
        for serial, values in enumerate(invalid):
            malformed = root / f"{app}_bad_{serial}.csv"
            write_csv(malformed, FIELDS, values)
            rejected = root / f"{app}_reject_{serial}"
            invoke(JOINT, app, data, rejected, "--structure-seeds", malformed, ok=False)
            assert not rejected.exists()
        for serial, fields in enumerate((FIELDS[:-1], (*FIELDS, "extra"), (*FIELDS[:-1], "lower"))):
            malformed = root / f"{app}_header_{serial}.csv"
            write_csv(malformed, fields, [])
            invoke(JOINT, app, data, root / f"{app}_reject_header_{serial}",
                   "--structure-seeds", malformed, ok=False)
        signed13 = root / f"{app}_signed13.csv"
        write_csv(signed13, FIELDS, [("s13", n, "baseline_output", 11, -4096, -4096) for n in (0, native - 1)])
        invoke(JOINT, app, data, root / f"{app}_signed13", *common, "--structure-seeds", signed13,
               "--structure-radius", 0, "--image-mode", "none")
        # Radius zero fixes installation even when untouched rows have zero hits.
        # Some inputs may yield no numeric candidate; every exported one must
        # preserve the full seeded structure and expose its dormant rows.
        installed_seed = root / f"{app}_installed_seed.csv"
        write_csv(installed_seed, FIELDS, [("installed", n, "input1", 0, -2048, -2048)
                                          for n in range(native)])
        installed_output = root / f"{app}_installed_discovery"
        one_context = list(common)
        one_context[one_context.index("--max-contexts") + 1] = 1
        invoke(JOINT, app, data, installed_output, *one_context, "--structure-seeds", installed_seed,
               "--structure-radius", 0, "--image-mode", "none")
        installed_rows = rows(installed_output / "configurations.csv")
        for candidate in rows(installed_output / "summary.csv"):
            if candidate["status"] != "numeric_eligible":
                continue
            configuration = [r for r in installed_rows if r["candidate_id"] == candidate["candidate_id"]]
            assert {int(r["node_id"]) for r in configuration} == set(range(native))
            assert all(r["monitor"] == "input1" and r["bit"] == "0" for r in configuration)
            assert any(int(r["dynamic_triggers"]) == 0 for r in configuration)
        for flag, value in (("--structure-radius", -1), ("--exploration-contexts", -1),
                            ("--structure-radius", "1x")):
            invoke(JOINT, app, data, root / f"{app}_bad_flag_{flag[2:]}_{value}", flag, value, ok=False)
        hardware = root / f"{app}_hardware.csv"
        fixed_seed = root / f"{app}_fixed_seed.csv"
        write_csv(hardware, ("node_id", "monitor", "bit"), [(n, "input1", 0) for n in range(native)])
        write_csv(fixed_seed, FIELDS, [("idle", n, "input1", 0, -2048, -2048) for n in range(native)])
        fixed_flags = ["--hardware", hardware, "--seeds", fixed_seed, "--beam", 1, "--refinement", 0,
                       "--restarts", 1, "--boundary-budget", 2, "--context-budget", 1,
                       "--max-full-evals", 64, "--full-candidates", 2, "--global-psnr", 0]
        continuous = root / f"{app}_continuous"
        invoke(FIXED, app, data, continuous, *fixed_flags, "--continuous", "--convergence-tolerance", "0.001")
        fixed_stats = rows(continuous / "search_stats.csv")[0]
        fixed_run = run_values(continuous)
        assert fixed_stats["continuous"] == fixed_run["continuous"] == "1"
        assert fixed_stats["stopped_for_convergence"] == "0"  # a partial first sweep cannot converge
        assert int(fixed_stats["completed_sweeps"]) == 0
        assert int(fixed_stats["contexts_built"]) <= 1
        for field in ("completed_sweeps", "stopped_for_convergence", "convergence_tolerance"):
            assert fixed_run[field] == fixed_stats[field]
        installed = rows(continuous / "configurations.csv")
        assert {int(row["node_id"]) for row in installed} == set(range(native))
        assert any(int(row["dynamic_triggers"]) == 0 for row in installed)
        assert all(row["monitor"] == "input1" and row["bit"] == "0" for row in installed)
        continuous_none = root / f"{app}_continuous_none"
        invoke(FIXED, app, data, continuous_none, *fixed_flags, "--continuous", "--image-mode", "none")
        assert metrics(continuous) == metrics(continuous_none)
        assert installed == rows(continuous_none / "configurations.csv")
        selected = rows(continuous / "summary.csv")[1]
        images = rows(continuous / "images.csv")
        assert len(images) == (3 if selected["status"] == "numeric_eligible" else 0)
        assert all((continuous / item["path"]).is_file() for item in images)
        assert not (continuous / "ablations.csv").exists()
        legacy_fixed = root / f"{app}_fixed_legacy"
        invoke(FIXED, app, data, legacy_fixed, *fixed_flags, "--image-mode", "none")
        assert rows(legacy_fixed / "search_stats.csv")[0]["continuous"] == "0"
        for serial, extra in enumerate((["--continuous", "--continuous"], ["--continuous", "1"],
                                       ["--convergence-tolerance", "nan"], ["--convergence-tolerance", "inf"],
                                       ["--convergence-tolerance", "-0.1"], ["--convergence-tolerance", "0.1x"])):
            invoke(FIXED, app, data, root / f"{app}_bad_continuous_{serial}", *fixed_flags, *extra, ok=False)

print("Continuation CLI contracts passed (local/global budgets, strict seeds, continuous fixed H and oracle replay)")
