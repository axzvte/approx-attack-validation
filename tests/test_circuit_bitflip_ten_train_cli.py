"""UNRUN: real CLI contract test; sole argument is TRAIN_EXE.

Only synthetic 16x16 grayscale training images 1..10 are created. No held-out
stage or image exists, so any accidental held-out image requirement fails.
"""
import csv
import math
from pathlib import Path
import subprocess
import sys
import tempfile


assert len(sys.argv) == 2, "Usage: test_circuit_bitflip_ten_train_cli.py TRAIN_EXE"
TRAIN_EXE = str(Path(sys.argv[1]).resolve())
FIELDS = {
    "training_baselines.csv": "application,image,global_mse,roi_mse,non_roi_mse,global_psnr,roi_psnr,non_roi_psnr",
    "candidate_hardware.csv": "hardware_id,node_id,monitor,bit",
    "training_ranking.csv": "hardware_id,rank,status,complete,evaluated_images,train_images,numeric_successes,worst_roi_drop_score,mean_roi_drop_score,installed_nodes,reason",
    "training_evaluations.csv": "application,hardware_id,image,status,global_mse,roi_mse,non_roi_mse,baseline_roi_mse,global_psnr,roi_psnr,non_roi_psnr,roi_drop_score,elapsed_seconds,contexts_built,full_evaluations,completed_sweeps,stopped_for_convergence,reference_cache_hit,baseline_cache_hit",
    "training_configurations.csv": "hardware_id,image,node_id,monitor,bit,lower,upper",
    "search_stats.csv": "actual_evaluations,duplicate_structures_skipped,reference_builds,preparation_seconds,search_seconds,stop_reason",
    "frozen_hardware.csv": "node_id,monitor,bit",
    "frozen_seeds.csv": "seed_id,node_id,monitor,bit,lower,upper",
}


def read(path):
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if path.name in FIELDS:
            assert reader.fieldnames == FIELDS[path.name].split(","), (path, reader.fieldnames)
        return list(reader)


def invoke(app, data, output, *options, ok=True):
    process = subprocess.run([TRAIN_EXE, app, str(data), str(output), *map(str, options)],
                             capture_output=True, text=True)
    assert (process.returncode == 0) == ok, (process.returncode, process.stdout, process.stderr)
    return process


def metadata(output):
    return dict(line.split("=", 1) for line in (output / "RUN.txt").read_text().splitlines() if "=" in line)


def config_tuple(row):
    return tuple(row[key] for key in ("node_id", "monitor", "bit", "lower", "upper"))


def inspect(output, application, native, budget, seed_limit=2):
    run = metadata(output)
    assert run["completed"] == "1" and run["application"] == application
    assert run["train_images"] == ",".join(map(str, range(1, 11)))
    tables = {name: read(output / name) for name in FIELDS if not name.startswith("frozen_")}
    assert not list(output.rglob("*.png"))
    baseline = tables["training_baselines.csv"]
    assert [int(row["image"]) for row in baseline] == list(range(1, 11))
    assert all(row["application"] == application for row in baseline)
    hardware = {}
    for row in tables["candidate_hardware.csv"]:
        hardware.setdefault(row["hardware_id"], []).append(tuple(row[k] for k in ("node_id", "monitor", "bit")))
    for nodes in hardware.values():
        assert 2 <= len(nodes) <= native
        assert len({int(node[0]) for node in nodes}) == len(nodes)
        assert all(0 <= int(node[0]) < native and 0 <= int(node[2]) <= 11 for node in nodes)
    evaluations = tables["training_evaluations.csv"]
    assert len(evaluations) == int(run["actual_evaluations"]) <= budget
    assert len({(row["hardware_id"], row["image"]) for row in evaluations}) == len(evaluations)
    configurations = tables["training_configurations.csv"]
    for row in evaluations:
        assert row["application"] == application and 1 <= int(row["image"]) <= 10
        config = [c for c in configurations if (c["hardware_id"], c["image"]) == (row["hardware_id"], row["image"])]
        if row["status"] == "no_legal_interval_state":
            assert not config
            assert all(row[k] == "" for k in ("global_mse", "roi_mse", "non_roi_mse", "baseline_roi_mse", "global_psnr", "roi_psnr", "non_roi_psnr", "roi_drop_score"))
        else:
            assert row["status"] in {"numeric_eligible", "quality_not_met"}
            # Every installed node is present even when its interval never triggers.
            assert sorted(tuple(c[k] for k in ("node_id", "monitor", "bit")) for c in config) == sorted(hardware[row["hardware_id"]])
            for c in config:
                lo, hi = (-4096, 4095) if c["monitor"] == "baseline_output" else (-2048, 2047)
                assert lo <= int(c["lower"]) <= int(c["upper"]) <= hi
            assert all(math.isfinite(float(row[k])) and float(row[k]) >= 0 for k in ("global_mse", "roi_mse", "non_roi_mse", "roi_drop_score"))
        assert all(row[k] in {"0", "1"} for k in ("stopped_for_convergence", "reference_cache_hit", "baseline_cache_hit"))
    ranking = tables["training_ranking.csv"]
    assert [int(row["rank"]) for row in ranking] == list(range(1, len(ranking) + 1))
    complete_seen_partial = False
    keys = []
    for row in ranking:
        found = [ev for ev in evaluations if ev["hardware_id"] == row["hardware_id"]]
        count = len(found)
        assert int(row["evaluated_images"]) == count and row["train_images"] == "10"
        assert int(row["numeric_successes"]) == sum(ev["status"] == "numeric_eligible" for ev in found)
        assert row["complete"] == str(int(count == 10))
        assert row["status"] in {"complete", "bound_eliminated", "budget_incomplete"}
        assert (row["status"] == "complete") == (count == 10)
        scores = [float(ev["roi_drop_score"]) if ev["status"] != "no_legal_interval_state" else 0 for ev in found]
        assert math.isclose(float(row["worst_roi_drop_score"]), min(scores, default=0), abs_tol=1e-9)
        # Partial rows retain their actual evaluated count; the aggregate mean
        # divides observed scores by the declared full ten-image train set.
        assert math.isclose(float(row["mean_roi_drop_score"]), sum(scores) / 10, abs_tol=1e-9)
        if count != 10:
            complete_seen_partial = True
        else:
            assert not complete_seen_partial
        native_key = tuple(sorted((int(node), monitor, int(bit)) for node, monitor, bit in hardware[row["hardware_id"]]))
        keys.append((count != 10, -int(row["numeric_successes"]), -float(row["worst_roi_drop_score"]), -float(row["mean_roi_drop_score"]), int(row["installed_nodes"]), native_key))
    assert keys == sorted(keys)
    stats = tables["search_stats.csv"]
    assert len(stats) == 1 and stats[0]["actual_evaluations"] == run["actual_evaluations"]
    assert int(run["reference_builds"]) == 10
    for key in ("preparation_seconds", "search_seconds"):
        assert math.isfinite(float(run[key])) and float(run[key]) >= 0
    if run["selection_status"] == "selected":
        selected = next(r for r in ranking if r["hardware_id"] == run["selected_hardware_id"])
        assert selected["rank"] == "1" and selected["complete"] == "1" and int(selected["numeric_successes"]) > 0
        frozen_h = read(output / "frozen_hardware.csv")
        assert sorted(tuple(r[k] for k in ("node_id", "monitor", "bit")) for r in frozen_h) == sorted(hardware[selected["hardware_id"]])
        frozen = read(output / "frozen_seeds.csv")
        seed_ids = sorted({r["seed_id"] for r in frozen})
        assert 1 <= len(seed_ids) <= seed_limit
        snapshots = []
        for seed_id in seed_ids:
            assert seed_id.startswith("train_") and seed_id.endswith("_ten")
            image = int(seed_id[6:8])
            assert seed_id == f"train_{image:02d}_ten"
            assert any(e["hardware_id"] == selected["hardware_id"] and int(e["image"]) == image and e["status"] == "numeric_eligible" for e in evaluations)
            expected = sorted(config_tuple(c) for c in configurations if c["hardware_id"] == selected["hardware_id"] and int(c["image"]) == image)
            actual = sorted(config_tuple(c) for c in frozen if c["seed_id"] == seed_id)
            assert actual == expected
            snapshots.append(actual)
        assert len({tuple(v) for v in snapshots}) == len(snapshots)
    else:
        assert run["selection_status"] == "no_numeric_hardware" and run["selected_hardware_id"] == ""
        assert not (output / "frozen_hardware.csv").exists() and not (output / "frozen_seeds.csv").exists()
    return tables


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    data = root / "data"
    (data / "stage1/input").mkdir(parents=True)
    (data / "mask").mkdir()
    for image in range(1, 11):
        payload = bytes(112 + (3 * y + 5 * x + x * y + image) % 32 for y in range(16) for x in range(16))
        (data / f"stage1/input/image_{image:02d}.jpg").write_bytes(b"P5\n16 16\n255\n" + payload)
    (data / "mask/roi_mask.jpg").write_bytes(b"P5\n16 16\n255\n" + bytes(255 if 4 <= y < 12 and 4 <= x < 12 else 127 for y in range(16) for x in range(16)))
    small = ["--train", "1:10", "--max-candidates", 1, "--initial-candidates", 1, "--proposals-per-round", 1,
             "--max-rounds", 0, "--patience", 1, "--workers", 1, "--context-budget", 1, "--beam", 1,
             "--refinement", 0, "--restarts", 1, "--boundary-budget", 2, "--max-full-evals", 8,
             "--full-candidates", 1, "--global-psnr", 0]
    for app, native in (("sobel", 11), ("conv", 8), ("dct", 32)):
        output = root / app
        invoke(app, data, output, *small, "--max-evaluations", 10)
        inspect(output, app, native, 10)
        original = (output / "RUN.txt").read_bytes()
        invoke(app, data, output, *small, ok=False)
        assert (output / "RUN.txt").read_bytes() == original
        partial = root / f"{app}_partial"
        invoke(app, data, partial, *small, "--max-evaluations", 3)
        partial_tables = inspect(partial, app, native, 3)
        assert all(r["complete"] == "0" and int(r["evaluated_images"]) <= 3 for r in partial_tables["training_ranking.csv"])
        assert metadata(partial)["selection_status"] == "no_numeric_hardware"
        impossible = list(small)
        impossible[impossible.index("--global-psnr") + 1] = 1000
        rejected = root / f"{app}_threshold"
        invoke(app, data, rejected, *impossible, "--max-evaluations", 10)
        inspect(rejected, app, native, 10)
        assert metadata(rejected)["selection_status"] == "no_numeric_hardware"
    # Multiple shared structures exercise the native order and a real partial
    # denominator after the first complete candidate. Two workers test batches.
    multiple = list(small)
    multiple[multiple.index("--max-candidates") + 1] = 3
    multiple[multiple.index("--initial-candidates") + 1] = 3
    multiple[multiple.index("--workers") + 1] = 2
    ordered = root / "sobel_multiple"
    invoke("sobel", data, ordered, *multiple, "--max-evaluations", 23, "--seed-limit", 1)
    many = inspect(ordered, "sobel", 11, 23, seed_limit=1)
    assert len(many["training_ranking.csv"]) >= 2
    # Also exercise the original resolver's flat stage1 fallback, still only
    # using the authorized training IDs.
    nested = data / "stage1/input/image_10.jpg"
    flat = data / "stage1/image_10.jpg"
    nested.rename(flat)
    flat_output = root / "flat_stage1"
    invoke("sobel", data, flat_output, *small, "--max-evaluations", 1)
    inspect(flat_output, "sobel", 11, 1)
    flat.rename(nested)
    for serial, options in enumerate((["--train", "1:9"], ["--train", "1:11"], ["--train", "31:40"],
                                     ["--image", 31], ["--screen", "11:20"], ["--workers", 0],
                                     ["--max-evaluations", -1], ["--seed-limit", 0], ["--seed-limit", 1, "--seed-limit", 2],
                                     ["--global-psnr", "nan"], ["--convergence-tolerance", "inf"], ["--boundary-budget", 1])):
        invoke("sobel", data, root / f"bad_{serial}", *options, ok=False)
    invoke("unknown", data, root / "unknown", ok=False)
    invoke("sobel", data, data / "new_output", ok=False)
    invoke("sobel", data, root, ok=False)
    invoke("sobel", data, root / "sources/new_output", ok=False)
    missing = data / "stage1/input/image_10.jpg"
    saved = missing.read_bytes()
    missing.unlink()
    failed = root / "missing_train"
    invoke("sobel", data, failed, *small, ok=False)
    assert metadata(failed)["completed"] == "0"
    assert not (failed / "frozen_hardware.csv").exists()
    missing.write_bytes(saved)

print("Ten-image training CLI contract passed")
