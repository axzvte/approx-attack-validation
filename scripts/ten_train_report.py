"""Validate native ten-image training evidence before any hardware is frozen.

No process is launched here. Native success alone is insufficient: every table
must agree on identities, bounded work, numerical outcomes and seed provenance.
"""
import csv
import math
from pathlib import Path
import re

from run_sharpen_bitflip_hardware import (
    APPLICATION_NODES, drop_score, hardware_key, interval_rows, read_csv,
)


METRIC_FIELDS = ("global_mse", "roi_mse", "non_roi_mse", "baseline_roi_mse",
                 "global_psnr", "roi_psnr", "non_roi_psnr", "roi_drop_score")
QUALITY_FIELDS = ("global_mse", "roi_mse", "non_roi_mse", "global_psnr", "roi_psnr", "non_roi_psnr")
CONFIG_FIELDS = ("node_id", "monitor", "bit", "lower", "upper")
STAT_FIELDS = ("actual_evaluations", "duplicate_structures_skipped", "reference_builds",
               "preparation_seconds", "search_seconds", "stop_reason")
SCHEDULE_FIELDS = ("scheduling", "initial_batch", "completion_reserve", "effective_completion_reserve",
                   "reference_evaluations", "exploration_evaluations", "completion_evaluations",
                   "completion_candidates", "evaluation_image_order")


def _integer(value, label, minimum=0):
    result = int(value)
    if result < minimum:
        raise ValueError(f"Invalid {label}")
    return result


def _number(value, label):
    result = float(value)
    if not math.isfinite(result) or result < 0:
        raise ValueError(f"Invalid {label}")
    return result


def _same(actual, expected, label):
    # 17-digit native output preserves the underlying values. The tolerance is
    # only for independently recomputed floating-point aggregations.
    if not math.isfinite(actual) or abs(actual - expected) > 1e-8:
        raise ValueError(f"Mismatched {label}")


def _psnr(mse):
    return 10 * math.log10(65025 / mse) if mse else math.inf


def _quality(row):
    values = {}
    for region in ("global", "roi", "non_roi"):
        mse = _number(row[region + "_mse"], region + "_mse")
        actual = float(row[region + "_psnr"])
        expected = _psnr(mse)
        if actual != expected:
            _same(actual, expected, region + "_psnr")
        values[region] = mse
    return values


def _notes(path):
    result = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if not line:
            continue
        if "=" not in line:
            raise ValueError("Malformed RUN.txt")
        key, value = line.split("=", 1)
        if not key or key in result:
            raise ValueError("Duplicate or empty RUN.txt key")
        result[key] = value
    return result


def _schedule(notes, stat, evaluations, train, max_evaluations, baseline_roi):
    # Old reports remain readable. Any new scheduling evidence requires the
    # entire schema; the runner separately requires it from the new executable.
    if not (any(field in notes or field in stat for field in SCHEDULE_FIELDS) or
            any("phase" in row for row in evaluations)):
        return
    for field in SCHEDULE_FIELDS:
        if field not in notes or field not in stat or notes[field] != stat[field]:
            raise ValueError(f"Missing or mismatched scheduling field: {field}")
    mode = notes["scheduling"]
    if mode not in ("interleaved", "sequential"):
        raise ValueError("Unknown training scheduling mode")
    batch = _integer(notes["initial_batch"], "initial_batch", 1)
    if batch > 10:
        raise ValueError("Initial batch exceeds the ten-image set")
    requested = _integer(notes["completion_reserve"], "completion_reserve")
    effective = _integer(notes["effective_completion_reserve"], "effective_completion_reserve")
    # Keep room for an alternative's initial batch when the total is small.
    expected = min(requested, max(0, max_evaluations - min(len(train), max_evaluations) - batch)) if mode == "interleaved" else 0
    if effective != expected:
        raise ValueError("Effective completion reserve disagrees with the budget")
    order = [int(value) for value in notes["evaluation_image_order"].split(",")]
    if sorted(order) != sorted(train) or (mode == "sequential" and order != sorted(train)):
        raise ValueError("Evaluation order is not a valid training-only permutation")
    if mode == "interleaved":
        difficulty = sorted(train, key=lambda image: (-baseline_roi[image], image))
        # The native entry requires ten images. Smaller pure report fixtures
        # keep sorted difficulty order intentionally; no native split is added.
        expected_order = ([difficulty[i] for i in (0,9,4,2,7,1,8,3,6,5)]
                          if len(train) == 10 else difficulty)
        if order != expected_order:
            raise ValueError("Interleaved order disagrees with original baseline ROI difficulty")
    for hid in {row["hardware_id"] for row in evaluations}:
        observed = {int(row["image"]) for row in evaluations if row["hardware_id"] == hid}
        if observed != set(order[:len(observed)]):
            raise ValueError("Candidate evaluations are not a prefix of the reported image order")
    counts = {phase: _integer(notes[phase + "_evaluations"], phase + "_evaluations")
              for phase in ("reference", "exploration", "completion")}
    if sum(counts.values()) != len(evaluations) or counts["reference"] != min(len(train), len(evaluations)):
        raise ValueError("Scheduling phase counts do not account for every pair")
    completed = _integer(notes["completion_candidates"], "completion_candidates")
    if mode == "sequential":
        if counts["completion"] or completed or any(row.get("phase") != "sequential" for row in evaluations):
            raise ValueError("Sequential scheduling falsely reports completion work")
        return
    for phase, count in counts.items():
        if sum(row.get("phase") == phase for row in evaluations) != count:
            raise ValueError("Per-pair phase labels disagree with scheduling counts")
    if any(row.get("phase") not in counts for row in evaluations):
        raise ValueError("Invalid per-pair scheduling phase")
    if counts["exploration"] > max_evaluations - effective - counts["reference"]:
        raise ValueError("Exploration consumed reserved completion budget")
    reference = {row["hardware_id"] for row in evaluations if row["phase"] == "reference"}
    if len(reference) != (1 if evaluations else 0):
        raise ValueError("First reference work must belong to one hardware candidate")
    if any(row["hardware_id"] in reference and row["phase"] != "reference" for row in evaluations):
        raise ValueError("Reference candidate was not evaluated as a complete first step")
    finishing = {row["hardware_id"] for row in evaluations if row["phase"] == "completion"}
    actual_completed = sum(sum(row["hardware_id"] == hid for row in evaluations) == len(train)
                           for hid in finishing)
    if completed != actual_completed:
        raise ValueError("Completion candidate count disagrees with actual pairs")


def validate_training_report(report: Path, application: str, train: list[int],
                             floor: float, max_evaluations: int, max_candidates: int) -> dict:
    """Return validated evidence, or raise ValueError for an unusable report.

    A complete report with no eligible complete hardware is a normal result;
    selected is then None. Missing files and malformed scalar values also raise
    ValueError so the caller can fail closed without starting the test phase.
    """
    try:
        return _validate(Path(report), application, train, floor, max_evaluations, max_candidates)
    except (OSError, UnicodeError, KeyError, TypeError, OverflowError, csv.Error) as error:
        raise ValueError(f"Malformed training report: {error}") from error


def _validate(report, application, train, floor, max_evaluations, max_candidates):
    if application not in ("conv", "sobel", "dct"):
        raise ValueError("Unsupported native application")
    if (not train or len(set(train)) != len(train) or
            any(type(image) is not int or not 1 <= image <= 10 for image in train)):
        raise ValueError("Training image identities must be distinct and inside1..10")
    if not math.isfinite(floor) or max_evaluations < 1 or max_candidates < 1:
        raise ValueError("Invalid quality floor or training budgets")
    allowed = set(train)
    native_nodes = APPLICATION_NODES[application]
    notes = _notes(report / "RUN.txt")
    if (notes.get("completed") != "1" or notes.get("application") != application or
            notes.get("train_images") != ",".join(map(str, train))):
        raise ValueError("Incomplete or mismatched native training report")
    for name in ("selection_status", "selected_hardware_id", "actual_evaluations",
                 "reference_builds", "stop_reason", "preparation_seconds", "search_seconds"):
        if name not in notes:
            raise ValueError(f"RUN.txt missing {name}")

    baselines = read_csv(report / "training_baselines.csv", ["application", "image", *QUALITY_FIELDS])
    hardware_rows = read_csv(report / "candidate_hardware.csv", ["hardware_id", "node_id", "monitor", "bit"])
    evaluations = read_csv(report / "training_evaluations.csv", [
        "application", "hardware_id", "image", "status", *METRIC_FIELDS,
        "elapsed_seconds", "contexts_built", "full_evaluations", "completed_sweeps",
        "stopped_for_convergence", "reference_cache_hit", "baseline_cache_hit"])
    configurations = read_csv(report / "training_configurations.csv", ["hardware_id", "image", *CONFIG_FIELDS])
    ranking = read_csv(report / "training_ranking.csv", [
        "hardware_id", "rank", "status", "complete", "evaluated_images", "train_images",
        "numeric_successes", "worst_roi_drop_score", "mean_roi_drop_score", "installed_nodes", "reason"])
    stats = read_csv(report / "search_stats.csv", STAT_FIELDS)

    def image_of(row):
        image = int(row["image"])
        if image not in allowed:
            raise ValueError("Training report contains an out-of-training image")
        if "application" in row and row["application"] != application:
            raise ValueError("Training table has the wrong application")
        return image

    baseline_roi = {}
    for row in baselines:
        image = image_of(row)
        if image in baseline_roi:
            raise ValueError("Duplicate baseline image")
        baseline_roi[image] = _quality(row)["roi"]
    if set(baseline_roi) != allowed:
        raise ValueError("Every training image needs one baseline")

    hardware_groups = {}
    for row in hardware_rows:
        if not re.fullmatch(r"(?:ten_)?h[0-9]+",row["hardware_id"]):
            raise ValueError("Invalid hardware identity for report output paths")
        hardware_groups.setdefault(row["hardware_id"], []).append(row)
    hardware = {hid: hardware_key(rows, native_nodes) for hid, rows in hardware_groups.items()}
    if len(hardware) > max_candidates or len(set(hardware.values())) != len(hardware):
        raise ValueError("Candidate budget exceeded or duplicate hardware structure")

    pair_rows = {}
    scores = {}
    for row in evaluations:
        hid = row["hardware_id"]
        image = image_of(row)
        pair = (hid, image)
        if hid not in hardware or pair in pair_rows:
            raise ValueError("Unknown hardware or duplicate hardware/image evaluation")
        pair_rows[pair] = row
        status = row["status"]
        if status not in ("numeric_eligible", "quality_not_met", "no_legal_interval_state"):
            raise ValueError("Unknown training evaluation status")
        _number(row["elapsed_seconds"], "elapsed_seconds")
        for field in ("contexts_built", "full_evaluations", "completed_sweeps"):
            _integer(row[field], field)
        for field in ("stopped_for_convergence", "reference_cache_hit", "baseline_cache_hit"):
            if row[field] not in ("0", "1"):
                raise ValueError(f"Invalid {field} flag")
        if status == "no_legal_interval_state":
            if any(row[field] != "" for field in METRIC_FIELDS):
                raise ValueError("No-legal-state metrics must be blank")
            scores[pair] = 0.0
            continue
        quality = _quality(row)
        baseline = _number(row["baseline_roi_mse"], "baseline_roi_mse")
        _same(baseline, baseline_roi[image], "baseline ROI MSE")
        eligible = _psnr(quality["global"]) + 1e-10 >= floor and quality["roi"] > baseline_roi[image] + 1e-10
        if (status == "numeric_eligible") != eligible:
            raise ValueError("Numeric eligibility disagrees with training metrics")
        score = drop_score(baseline_roi[image], quality["roi"]) if eligible else 0.0
        _same(_number(row["roi_drop_score"], "roi_drop_score"), score, "ROI drop score")
        scores[pair] = score

    actual = _integer(notes["actual_evaluations"], "actual_evaluations")
    if actual != len(pair_rows) or actual > max_evaluations:
        raise ValueError("Training evaluation count disagrees with rows or exceeds budget")
    if len(stats) != 1:
        raise ValueError("Expected one search statistics row")
    stat = stats[0]
    if _integer(stat["actual_evaluations"], "actual_evaluations") != actual:
        raise ValueError("Search statistics evaluation count mismatch")
    _integer(stat["duplicate_structures_skipped"], "duplicate_structures_skipped")
    if (_integer(notes["reference_builds"], "reference_builds") != len(train) or
            _integer(stat["reference_builds"], "reference_builds") != len(train)):
        raise ValueError("Prepared references do not match the exact training set")
    for field in ("preparation_seconds", "search_seconds"):
        _same(_number(stat[field], field), _number(notes[field], field), field)
    if not notes["stop_reason"] or stat["stop_reason"] != notes["stop_reason"]:
        raise ValueError("Search stop reason mismatch")
    _schedule(notes, stat, evaluations, train, max_evaluations, baseline_roi)

    config_groups = {}
    for row in configurations:
        pair = (row["hardware_id"], image_of(row))
        if pair not in pair_rows or pair_rows[pair]["status"] == "no_legal_interval_state":
            raise ValueError("Configuration is not attached to a legal evaluation")
        config_groups.setdefault(pair, []).append(row)
    canonical_configs = {}
    for pair, row in pair_rows.items():
        if row["status"] == "no_legal_interval_state":
            continue
        key, rows = interval_rows(config_groups.get(pair, []), native_nodes)
        if key != hardware[pair[0]]:
            raise ValueError("Training evaluation changed its full installed hardware")
        canonical_configs[pair] = rows

    derived = {}
    for row in ranking:
        hid = row["hardware_id"]
        if hid not in hardware or hid in derived:
            raise ValueError("Unknown or duplicate ranking hardware")
        pairs = sorted((pair for pair in pair_rows if pair[0] == hid), key=lambda pair: pair[1])
        complete = len(pairs) == len(train)
        if row["complete"] != str(int(complete)):
            raise ValueError("Ranking completeness disagrees with actual evaluations")
        if row["status"] not in ("complete", "bound_eliminated", "budget_incomplete") or (row["status"] == "complete") != complete:
            raise ValueError("Ranking status disagrees with completeness")
        successes = sum(pair_rows[p]["status"] == "numeric_eligible" for p in pairs)
        for field, expected in (("evaluated_images", len(pairs)), ("train_images", len(train)),
                                ("numeric_successes", successes), ("installed_nodes", len(hardware[hid]))):
            if _integer(row[field], field) != expected:
                raise ValueError(f"Ranking {field} mismatch")
        observed = [scores[p] for p in pairs]
        worst = min(observed, default=0.0)
        mean = sum(observed) / len(train)
        _same(_number(row["worst_roi_drop_score"], "worst_roi_drop_score"), worst, "worst ROI drop score")
        _same(_number(row["mean_roi_drop_score"], "mean_roi_drop_score"), mean, "mean ROI drop score")
        derived[hid] = (-int(complete), -successes, -worst, -mean, len(hardware[hid]), hardware[hid])
    if set(derived) != set(hardware):
        raise ValueError("Ranking must cover every hardware candidate")
    order = sorted(hardware, key=lambda hid: derived[hid])
    ranks = {hid: i for i, hid in enumerate(order, 1)}
    if any(_integer(row["rank"], "rank", 1) != ranks[row["hardware_id"]] for row in ranking):
        raise ValueError("Training rank order disagrees with recomputed complete-first ranking")

    complete_numeric = [hid for hid in order if derived[hid][0] == -1 and derived[hid][1] < 0]
    selected = None
    seed_rows = []
    if notes["selection_status"] == "selected":
        hid = notes["selected_hardware_id"]
        if not complete_numeric or hid != complete_numeric[0]:
            raise ValueError("Selected hardware is not the best complete numeric candidate")
        frozen = read_csv(report / "frozen_hardware.csv", ["node_id", "monitor", "bit"])
        if hardware_key(frozen, native_nodes) != hardware[hid]:
            raise ValueError("Frozen hardware changed selected native hardware")
        seed_rows = read_csv(report / "frozen_seeds.csv", ["seed_id", *CONFIG_FIELDS])
        if not seed_rows:
            raise ValueError("Selected hardware requires an eligible training seed")
        seed_groups = {}
        for row in seed_rows:
            match = re.fullmatch(r"train_(\d{2})_ten", row["seed_id"])
            if not match or int(match[1]) not in allowed:
                raise ValueError("Frozen seed identity is not from the training set")
            pair = (hid, int(match[1]))
            if pair not in pair_rows or pair_rows[pair]["status"] != "numeric_eligible":
                raise ValueError("Frozen seed does not identify an eligible selected-hardware outcome")
            seed_groups.setdefault(row["seed_id"], []).append(row)
        distinct = set()
        for seed_id, rows in seed_groups.items():
            key, canonical = interval_rows(rows, native_nodes)
            image = int(re.fullmatch(r"train_(\d{2})_ten", seed_id)[1])
            if key != hardware[hid] or canonical != canonical_configs[(hid, image)]:
                raise ValueError("Frozen seed changed the original training configuration")
            identity = tuple(tuple(row[field] for field in CONFIG_FIELDS) for row in canonical)
            if identity in distinct:
                raise ValueError("Frozen seed bank repeats an identical configuration")
            distinct.add(identity)
        selected = dict(hardware_id=hid, key=hardware[hid])
    elif notes["selection_status"] == "no_numeric_hardware":
        if notes["selected_hardware_id"] or complete_numeric:
            raise ValueError("No-numeric selection status contradicts complete training evidence")
        for name, fields in (("frozen_hardware.csv", ["node_id", "monitor", "bit"]),
                             ("frozen_seeds.csv", ["seed_id", *CONFIG_FIELDS])):
            path = report / name
            if path.exists() and read_csv(path, fields):
                raise ValueError("No selected hardware may not export a frozen structure or seed bank")
    else:
        raise ValueError("Unknown hardware selection status")
    return dict(notes=notes, selected=selected, hardware=hardware, evaluations=evaluations,
                configurations=configurations, ranking=ranking, seed_rows=seed_rows)
