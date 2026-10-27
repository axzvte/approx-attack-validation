"""UNRUN: user-run real CLI integration; pass the built executable as argv[1].

Synthetic PGM payloads deliberately use .jpg names, as the existing CLI tests do.
No repository data or experiment executable is accessed while authoring this file.
"""
import csv
import math
from pathlib import Path
import subprocess
import sys
import tempfile


EXE = str(Path(sys.argv[1]).resolve())


def rows(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def write_csv(path, fields, values):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(fields)
        writer.writerows(values)


def invoke(app, data, output, *options, ok=True):
    result = subprocess.run(
        [EXE, app, str(data), str(output), *map(str, options)],
        capture_output=True, text=True,
    )
    assert (result.returncode == 0) == ok, (result.returncode, result.stdout, result.stderr)
    return result


def metrics(output):
    fields = ("candidate_id", "status", "hardware_key", "installed_nodes", "triggered_nodes",
              "global_mse", "roi_mse", "non_roi_mse", "delta_roi_mse", "delta_non_roi_mse",
              "roi_changed_fraction", "non_roi_changed_fraction", "roi_error_ge16_fraction",
              "non_roi_error_ge16_fraction", "roi_change_ge16_fraction", "non_roi_change_ge16_fraction")
    return [tuple(row[field] for field in fields) for row in rows(output / "summary.csv")]


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    data = root / "data"
    (data / "stage1/input").mkdir(parents=True)
    (data / "stage4").mkdir()
    (data / "mask").mkdir()
    # 16x16 is an exact DCT block multiple. Low contrast keeps the native baseline legal.
    image = b"P5\n16 16\n255\n" + bytes(
        112 + (3 * y + 5 * x + x * y) % 32 for y in range(16) for x in range(16)
    )
    for path in (data / "stage1/input/image_01.jpg", data / "stage4/image_31.jpg",
                 data / "stage4/image_40.jpg"):
        path.write_bytes(image)
    # Values 1 and 127 are JPEG-like halos and must remain background in DCT diagnostics.
    mask = bytes(255 if 4 <= y < 12 and 4 <= x < 12 else (1 if x % 2 else 127)
                 for y in range(16) for x in range(16))
    (data / "mask/roi_mask.jpg").write_bytes(b"P5\n16 16\n255\n" + mask)
    (data / "stage5").mkdir()
    new_image = data / "stage5/1.jpg"
    new_image.write_bytes(image)

    for app, native in (("sharpen", 4), ("conv", 8), ("sobel", 11), ("dct", 32)):
        hardware = root / (app + "_hardware.csv")
        seed = root / (app + "_seeds.csv")
        write_csv(hardware, ("node_id", "monitor", "bit"),
                  [(node, "input1", 0) for node in range(native)])
        write_csv(seed, ("seed_id", "node_id", "monitor", "bit", "lower", "upper"),
                  [("train_only_dormant", node, "input1", 0, -2048, -2048) for node in range(native)])
        expected_key = "".join(f"{node}:input1:0;" for node in range(native))
        common = ["--hardware", hardware, "--seeds", seed, "--beam", 1, "--refinement", 0,
                  "--restarts", 1, "--boundary-budget", 4, "--context-budget", native + 2,
                  "--max-full-evals", 2 * native + 8, "--full-candidates", 2, "--global-psnr", 0]
        compact = root / app
        invoke(app, data, compact, *common)
        run = (compact / "RUN.txt").read_text()
        assert "completed=1" in run and f"application={app}\n" in run
        assert f"hardware_key={expected_key}\n" in run
        summary = rows(compact / "summary.csv")
        assert [row["candidate_id"] for row in summary] == ["baseline", "selected"]
        selected = summary[1]
        assert selected["status"] in {"numeric_eligible", "quality_not_met"}
        assert selected["hardware_key"] == expected_key
        assert int(selected["installed_nodes"]) == native
        configs = rows(compact / "configurations.csv")
        # Catch Sharpen's old four-node limit and accidental zero-hit hardware removal.
        assert {int(row["node_id"]) for row in configs} == set(range(native))
        assert all(row["monitor"] == "input1" and row["bit"] == "0" for row in configs)
        assert all(-2048 <= int(row["lower"]) <= int(row["upper"]) <= 2047 for row in configs)
        assert int(selected["triggered_nodes"]) == sum(int(row["dynamic_triggers"]) > 0 for row in configs)
        assert all(row["trigger_region_basis"] == ("block_roi_weight" if app == "dct" else "pixel_roi")
                   for row in configs)
        if app == "dct":
            # Every 8x8 block has exactly sixteen thresholded ROI pixels, hence weight 1/4.
            for row in configs:
                assert math.isclose(float(row["roi_triggers"]), int(row["dynamic_triggers"]) / 4)
                assert math.isclose(float(row["non_roi_triggers"]), int(row["dynamic_triggers"]) * 3 / 4)
        stats = rows(compact / "search_stats.csv")[0]
        for field in ("reference_seconds", "initialization_seconds", "trace_seconds", "profile_seconds",
                      "interval_seconds", "final_replay_seconds", "full_evaluation_seconds", "calibration_seconds"):
            assert math.isfinite(float(stats[field])) and float(stats[field]) >= 0
        assert int(stats["full_evaluations"]) <= 2 * native + 8
        assert int(stats["final_replay_evaluations"]) >= 1
        assert int(stats["accepted_seeds"]) >= 1
        visited = {int(node) for node in stats["context_nodes"].split(";") if node}
        assert visited == set(range(native)), stats
        for node in range(native):
            assert int(stats[f"contexts_node_{node}"]) >= 1
        manifest = rows(compact / "images.csv")
        eligible = selected["status"] == "numeric_eligible"
        assert len(manifest) == (3 if eligible else 0)
        if eligible:
            assert selected["visual_status"] == "pending_user_review"
            assert {row["image_kind"] for row in manifest} == {"output", "roi_comparison", "background_comparison"}
            assert float(selected["roi_mse"]) > float(summary[0]["roi_mse"])
        assert len(list(compact.glob("*.png"))) == len(manifest)
        assert all((compact / row["path"]).stat().st_size > 0 for row in manifest)
        assert not (compact / "ablations.csv").exists()
        for mode in ("none", "full"):
            output = root / f"{app}_{mode}"
            invoke(app, data, output, *common, "--image-mode", mode)
            assert metrics(output) == metrics(compact)
            assert rows(output / "configurations.csv") == configs
            if mode == "none":
                assert not list(output.glob("*.png")) and rows(output / "images.csv") == []
            else:
                assert (output / "baseline.png").is_file() and (output / "exact.png").is_file()
        for index in (31, 40):
            output = root / f"{app}_flat_{index}"
            invoke(app, data, output, *common, "--image", index, "--image-mode", "none")
            assert metrics(output) == metrics(compact)
        # Under a one-context budget, untouched installed nodes must still be exported with zero hits.
        sparse = root / f"{app}_zero_hits"
        limited = list(common)
        limited[limited.index("--context-budget") + 1] = 1
        invoke(app, data, sparse, *limited, "--image-mode", "none")
        zero_configs = rows(sparse / "configurations.csv")
        assert len(zero_configs) == native and any(int(row["dynamic_triggers"]) == 0 for row in zero_configs)
        impossible = list(limited)
        impossible[impossible.index("--global-psnr") + 1] = 1000
        failure = root / f"{app}_quality_failure"
        invoke(app, data, failure, *impossible)
        failed = rows(failure / "summary.csv")[1]
        assert failed["status"] == "quality_not_met" and failed["visual_status"] == "not_eligible"
        assert failed["global_mse"] != "" and len(rows(failure / "configurations.csv")) == native
        assert not list(failure.glob("*.png"))
        # Explicit input must work without routing this diagnostic id through
        # the legacy1..40 resolver. A new input may share an old filename/id.
        comparisons = root / f"{app}_new_input"
        invoke(app, data, comparisons, *common, "--input-image", new_image,
               "--image", 65, "--image-mode", "comparisons")
        assert metrics(comparisons) == metrics(compact)
        notes = (comparisons / "RUN.txt").read_text()
        assert f"input_image={new_image}\n" in notes and "image=65\n" in notes
        assert {p.name for p in comparisons.glob("*.png")} == {
            "selected_comparison.png", "selected_roi_comparison.png"}
        assert {r["image_kind"] for r in rows(comparisons / "images.csv")} == {
            "comparison", "roi_comparison"}
        assert rows(comparisons / "summary.csv")[1]["images_exported"] == "1"
        # Below-threshold images stay available for human inspection, with the
        # actual numeric failure status retained rather than relabelled.
        failed_comparison = root / f"{app}_failed_comparison"
        invoke(app, data, failed_comparison, *impossible, "--input-image", new_image,
               "--image-mode", "comparisons")
        assert rows(failed_comparison / "summary.csv")[1]["status"] == "quality_not_met"
        assert len(list(failed_comparison.glob("*.png"))) == 2
        missing_explicit = root / f"{app}_missing_explicit"
        invoke(app, data, missing_explicit, *common, "--input-image", data / "stage5/missing.jpg", ok=False)
        assert "completed=1" not in (missing_explicit / "RUN.txt").read_text()
        before = (compact / "summary.csv").read_bytes()
        invoke(app, data, compact, *common, ok=False)
        assert (compact / "summary.csv").read_bytes() == before

        invalid_hardware = [
            [(0, "input1", 0)], [(0, "input1", 0), (0, "input2", 1)],
            [(0, "input1", 0), (native, "input1", 0)],
            [(0, "input1", 0), (1, "input1", 12)],
            [(0, "unknown", 0), (1, "input1", 0)],
        ]
        for serial, values in enumerate(invalid_hardware):
            bad = root / f"{app}_bad_h_{serial}.csv"
            write_csv(bad, ("node_id", "monitor", "bit"), values)
            invoke(app, data, root / f"{app}_reject_h_{serial}", "--hardware", bad, ok=False)
        valid_rows = [("s", node, "input1", 0, -2048, -2048) for node in range(native)]
        bad_seeds = [valid_rows[:-1], valid_rows + [valid_rows[0]],
                     [("s", 0, "input2", 0, -2048, -2048), *valid_rows[1:]],
                     [("s", 0, "input1", 1, -2048, -2048), *valid_rows[1:]],
                     [("s", 0, "input1", 0, -2049, -2048), *valid_rows[1:]],
                     [("s", 0, "input1", 0, 1, 0), *valid_rows[1:]]]
        for serial, values in enumerate(bad_seeds):
            bad = root / f"{app}_bad_seed_{serial}.csv"
            write_csv(bad, ("seed_id", "node_id", "monitor", "bit", "lower", "upper"), values)
            invoke(app, data, root / f"{app}_reject_seed_{serial}", "--hardware", hardware,
                   "--seeds", bad, ok=False)
        # A baseline-output monitor is signed13, rather than an input's signed12.
        output_hardware = root / f"{app}_output_hardware.csv"
        output_seed = root / f"{app}_output_seed.csv"
        output_nodes = (0, native - 1)
        write_csv(output_hardware, ("node_id", "monitor", "bit"),
                  [(node, "baseline_output", 11) for node in output_nodes])
        write_csv(output_seed, ("seed_id", "node_id", "monitor", "bit", "lower", "upper"),
                  [("s13", node, "baseline_output", 11, -4096, -4096) for node in output_nodes])
        invoke(app, data, root / f"{app}_signed13", "--hardware", output_hardware, "--seeds", output_seed,
               "--context-budget", 1, "--max-full-evals", 8, "--full-candidates", 1,
               "--boundary-budget", 2, "--beam", 1, "--refinement", 0, "--restarts", 1,
               "--image-mode", "none")
        write_csv(output_seed, ("seed_id", "node_id", "monitor", "bit", "lower", "upper"),
                  [("bad_s13", node, "baseline_output", 11, 4096, 4096) for node in output_nodes])
        invoke(app, data, root / f"{app}_reject_signed13", "--hardware", output_hardware,
               "--seeds", output_seed, ok=False)
        invoke(app, data, root / f"{app}_image_41", *common, "--image", 41, ok=False)
        invoke(app, data, root / f"{app}_duplicate", *common, "--beam", 2, ok=False)
        invoke(app, data, root / f"{app}_enable", *common, "--enable", 1, ok=False)
        absent = root / f"{app}_missing_input"
        invoke(app, data, absent, *common, "--image", 2, ok=False)
        assert "completed=1" not in (absent / "RUN.txt").read_text()
        assert not (absent / "summary.csv").exists()
    invoke("unknown", data, root / "unknown", "--hardware", hardware, ok=False)

print("Fixed circuit CLI integration passed (native hardware, replay, budgets, exports and validation)")
