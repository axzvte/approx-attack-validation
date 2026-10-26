"""Pure report fixtures; authored before implementation and intentionally unrun."""
import copy
import csv
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from ten_train_report import validate_training_report


class TrainingReportTests(unittest.TestCase):
    def fixture(self):
        notes = dict(completed="1", application="sobel", train_images="1,2,3",
                     selection_status="selected", selected_hardware_id="h1",
                     actual_evaluations="3", reference_builds="3", stop_reason="budget",
                     preparation_seconds="0.1", search_seconds="1")
        def quality(g, r, n):
            return dict(global_mse=g, roi_mse=r, non_roi_mse=n,
                        global_psnr=10*math.log10(65025/g),
                        roi_psnr=10*math.log10(65025/r),
                        non_roi_psnr=10*math.log10(65025/n))
        hardware = [dict(hardware_id="h1", node_id=n, monitor="input1", bit=1)
                    for n in (0, 10)]
        configurations = [dict(hardware_id="h1", image=i, node_id=n, monitor="input1",
                               bit=1, lower=i, upper=i+1) for i in (1, 2) for n in (0, 10)]
        evaluations = []
        for i, status in ((1, "numeric_eligible"), (2, "quality_not_met"),
                          (3, "no_legal_interval_state")):
            metrics = quality(60, 200 if i == 1 else 10, 40)
            metrics.update(baseline_roi_mse=20, roi_drop_score=10 if i == 1 else 0)
            if i == 3:
                metrics = {key: "" for key in metrics}
            evaluations.append(dict(application="sobel", hardware_id="h1", image=i,
                                    status=status, **metrics, elapsed_seconds=.1,
                                    contexts_built=2, full_evaluations=0, completed_sweeps=2,
                                    stopped_for_convergence=1, reference_cache_hit=1,
                                    baseline_cache_hit=1))
        tables = {
            "training_baselines.csv": [dict(application="sobel", image=i, **quality(20, 20, 20))
                                       for i in (1, 2, 3)],
            "candidate_hardware.csv": hardware,
            "training_configurations.csv": configurations,
            "training_evaluations.csv": evaluations,
            "training_ranking.csv": [dict(hardware_id="h1", rank=1, status="complete", complete=1,
                                           evaluated_images=3, train_images=3, numeric_successes=1,
                                           worst_roi_drop_score=0, mean_roi_drop_score=10/3,
                                           installed_nodes=2, reason="")],
            "frozen_hardware.csv": [{k: v for k, v in r.items() if k != "hardware_id"}
                                    for r in hardware],
            "frozen_seeds.csv": [dict(seed_id="train_01_ten", **{k: v for k, v in r.items()
                                        if k not in ("hardware_id", "image")})
                                 for r in configurations if r["image"] == 1],
            "search_stats.csv": [dict(actual_evaluations=3, duplicate_structures_skipped=0,
                                       reference_builds=3, preparation_seconds=.1,
                                       search_seconds=1, stop_reason="budget")],
        }
        return notes, tables

    def validate(self, notes, tables, **options):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp)
            (report / "RUN.txt").write_text("".join(f"{k}={v}\n" for k, v in notes.items()), encoding="utf-8")
            for name, rows in tables.items():
                with (report / name).open("w", newline="", encoding="utf-8") as stream:
                    writer = csv.DictWriter(stream, list(rows[0]))
                    writer.writeheader(); writer.writerows(rows)
            return validate_training_report(report, "sobel", [1, 2, 3], 30,
                                            options.get("max_evaluations", 8),
                                            options.get("max_candidates", 4))

    def test_valid_complete_report_retains_full_h_and_only_numeric_seed(self):
        result = self.validate(*self.fixture())
        self.assertEqual(result["selected"], dict(hardware_id="h1", key=((0, "input1", 1), (10, "input1", 1))))
        self.assertEqual(len(result["evaluations"]), 3)
        self.assertEqual(len(result["configurations"]), 4)
        self.assertEqual({r["seed_id"] for r in result["seed_rows"]}, {"train_01_ten"})

    def scheduled_fixture(self, scheduling="interleaved"):
        notes, tables = self.fixture()
        diagnostics = dict(scheduling=scheduling, initial_batch="2", completion_reserve="5",
                           effective_completion_reserve="3" if scheduling == "interleaved" else "0",
                           reference_evaluations="3", exploration_evaluations="0",
                           completion_evaluations="0", completion_candidates="0",
                           evaluation_image_order="1,2,3")
        notes.update(diagnostics); tables["search_stats.csv"][0].update(diagnostics)
        for row in tables["training_evaluations.csv"]:
            row["phase"] = "reference" if scheduling == "interleaved" else "sequential"
        return notes, tables

    def test_schedule_evidence_and_legacy_reports_are_supported(self):
        for scheduling in ("interleaved", "sequential"):
            self.assertIsNotNone(self.validate(*self.scheduled_fixture(scheduling))["selected"])
        self.assertIsNotNone(self.validate(*self.fixture())["selected"])

    def test_false_or_incomplete_schedule_evidence_is_rejected(self):
        for fault in ("missing", "stats", "counts", "reserve", "order", "permutation", "phase", "completion"):
            with self.subTest(fault=fault):
                notes, tables = self.scheduled_fixture()
                if fault == "missing": del notes["initial_batch"]
                elif fault == "stats": tables["search_stats.csv"][0]["scheduling"] = "sequential"
                elif fault == "counts":
                    notes["exploration_evaluations"] = "1"
                    tables["search_stats.csv"][0]["exploration_evaluations"] = "1"
                elif fault == "reserve":
                    notes["effective_completion_reserve"] = "6"
                    tables["search_stats.csv"][0]["effective_completion_reserve"] = "6"
                elif fault == "order":
                    notes["evaluation_image_order"] = "1,2,2"
                    tables["search_stats.csv"][0]["evaluation_image_order"] = "1,2,2"
                elif fault == "permutation":
                    notes["evaluation_image_order"] = "1,3,2"
                    tables["search_stats.csv"][0]["evaluation_image_order"] = "1,3,2"
                elif fault == "phase": tables["training_evaluations.csv"][0]["phase"] = "completion"
                else:
                    notes["completion_candidates"] = "1"
                    tables["search_stats.csv"][0]["completion_candidates"] = "1"
                with self.assertRaises(ValueError): self.validate(notes, tables)

    def test_hardware_identity_cannot_escape_report_output_paths(self):
        for identity in ('../outside','x/y','C:target','h1\\outside','sources'):
            notes,tables=self.fixture()
            for row in tables['candidate_hardware.csv']:row['hardware_id']=identity
            with self.assertRaises(ValueError):self.validate(notes,tables)

    def test_duplicate_pair_and_false_budget_accounting_are_rejected(self):
        for fault in ("duplicate", "run_count", "stats_count", "budget"):
            with self.subTest(fault=fault):
                notes, tables = self.fixture()
                if fault == "duplicate":
                    tables["training_evaluations.csv"].append(copy.copy(tables["training_evaluations.csv"][0]))
                elif fault == "run_count": notes["actual_evaluations"] = "2"
                elif fault == "stats_count": tables["search_stats.csv"][0]["actual_evaluations"] = 2
                with self.assertRaises(ValueError): self.validate(notes, tables, max_evaluations=2 if fault == "budget" else 8)

    def test_application_split_and_baseline_mismatch_are_rejected(self):
        for fault in ("run_app", "run_split", "baseline_app", "baseline_duplicate", "eval_image", "config_image", "baseline_roi", "nan"):
            with self.subTest(fault=fault):
                notes, tables = self.fixture()
                if fault == "run_app": notes["application"] = "dct"
                elif fault == "run_split": notes["train_images"] = "1,3,2"
                elif fault == "baseline_app": tables["training_baselines.csv"][0]["application"] = "conv"
                elif fault == "baseline_duplicate": tables["training_baselines.csv"][1]["image"] = 1
                elif fault == "eval_image": tables["training_evaluations.csv"][0]["image"] = 31
                elif fault == "config_image": tables["training_configurations.csv"][0]["image"] = 31
                elif fault == "baseline_roi": tables["training_evaluations.csv"][0]["baseline_roi_mse"] = 21
                else: tables["training_baselines.csv"][0]["global_mse"] = "nan"
                with self.assertRaises(ValueError): self.validate(notes, tables)

    def test_status_score_and_configuration_domains_are_derived(self):
        for fault in ("false_numeric", "false_quality", "false_score", "missing_node", "duplicate_node", "wrong_h", "range", "bit", "monitor", "no_legal_config", "no_legal_metric"):
            with self.subTest(fault=fault):
                notes, tables = self.fixture(); evaluations = tables["training_evaluations.csv"]; configs = tables["training_configurations.csv"]
                if fault == "false_numeric": evaluations[1]["status"] = "numeric_eligible"
                elif fault == "false_quality": evaluations[0]["status"] = "quality_not_met"
                elif fault == "false_score": evaluations[0]["roi_drop_score"] = 11
                elif fault == "missing_node": configs.pop(0)
                elif fault == "duplicate_node": configs.append(copy.copy(configs[0]))
                elif fault == "wrong_h": configs[0]["node_id"] = 1
                elif fault == "range": configs[0]["lower"] = -2049
                elif fault == "bit": tables["candidate_hardware.csv"][0]["bit"] = 12
                elif fault == "monitor": tables["candidate_hardware.csv"][0]["monitor"] = "output"
                elif fault == "no_legal_config": configs.append(dict(configs[0], image=3))
                else: evaluations[2]["global_mse"] = 0
                with self.assertRaises(ValueError): self.validate(notes, tables)

    def test_full_rank_counts_aggregates_and_completeness_are_checked(self):
        for field, value in (("rank", 2), ("numeric_successes", 2), ("mean_roi_drop_score", 10),
                             ("worst_roi_drop_score", 10), ("evaluated_images", 2),
                             ("complete", 0), ("status", "budget_incomplete"), ("installed_nodes", 3)):
            with self.subTest(field=field):
                notes, tables = self.fixture(); tables["training_ranking.csv"][0][field] = value
                with self.assertRaises(ValueError): self.validate(notes, tables)

    def test_frozen_seed_must_exactly_match_eligible_training_configuration(self):
        for fault in ("heldout", "quality_failed", "changed_interval", "duplicate", "changed_bit", "wrong_frozen_h"):
            with self.subTest(fault=fault):
                notes, tables = self.fixture(); seeds = tables["frozen_seeds.csv"]
                if fault == "heldout":
                    for r in seeds: r["seed_id"] = "train_31_ten"
                elif fault == "quality_failed":
                    for r in seeds: r["seed_id"] = "train_02_ten"
                elif fault == "changed_interval": seeds[0]["lower"] = 0
                elif fault == "duplicate": seeds.append(copy.copy(seeds[0]))
                elif fault == "changed_bit": seeds[0]["bit"] = 2
                else: tables["frozen_hardware.csv"][0]["node_id"] = 1
                with self.assertRaises(ValueError): self.validate(notes, tables)

    def test_partial_candidate_with_numeric_outcome_is_valid_but_never_selected(self):
        notes, tables = self.fixture()
        tables["training_evaluations.csv"] = tables["training_evaluations.csv"][:1]
        tables["training_configurations.csv"] = tables["training_configurations.csv"][:2]
        tables["training_ranking.csv"][0].update(complete=0, status="budget_incomplete", evaluated_images=1, worst_roi_drop_score=10)
        notes.update(selection_status="no_numeric_hardware", selected_hardware_id="", actual_evaluations="1")
        tables["search_stats.csv"][0]["actual_evaluations"] = 1
        del tables["frozen_hardware.csv"]; del tables["frozen_seeds.csv"]
        self.assertIsNone(self.validate(notes, tables)["selected"])
        notes.update(selection_status="selected", selected_hardware_id="h1")
        with self.assertRaises(ValueError): self.validate(notes, tables)

    def test_zero_success_complete_candidate_is_normal_no_selection(self):
        notes, tables = self.fixture()
        tables["training_evaluations.csv"][0] = dict(tables["training_evaluations.csv"][1], image=1)
        tables["training_ranking.csv"][0].update(numeric_successes=0, mean_roi_drop_score=0)
        notes.update(selection_status="no_numeric_hardware", selected_hardware_id="")
        del tables["frozen_hardware.csv"]; del tables["frozen_seeds.csv"]
        self.assertIsNone(self.validate(notes, tables)["selected"])

    def test_better_complete_hardware_cannot_be_hidden_by_exported_rank(self):
        notes, tables = self.fixture()
        for name in ("candidate_hardware.csv", "training_evaluations.csv", "training_configurations.csv"):
            other = [dict(row, hardware_id="h2") for row in tables[name]]
            if name in ("candidate_hardware.csv", "training_configurations.csv"):
                for row in other:
                    if row["node_id"] == 10: row["node_id"] = 9
            tables[name].extend(other)
        tables["training_ranking.csv"].append(dict(tables["training_ranking.csv"][0], hardware_id="h2", rank=2))
        notes["actual_evaluations"] = "6"; tables["search_stats.csv"][0]["actual_evaluations"] = 6
        # The exact tie belongs to node9 before node10; both the false ranks and
        # the selected h1 must be rejected even though h1 is complete and numeric.
        with self.assertRaises(ValueError): self.validate(notes, tables)
        tables["training_ranking.csv"][0]["rank"] = 2
        tables["training_ranking.csv"][1]["rank"] = 1
        with self.assertRaises(ValueError): self.validate(notes, tables)
        with self.assertRaises(ValueError): self.validate(notes, tables, max_candidates=1)

    def test_missing_report_is_a_value_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                validate_training_report(Path(tmp), "sobel", [1, 2, 3], 30, 8, 4)

    def test_zero_baseline_uses_infinite_psnr_and_the_100db_ranking_cap(self):
        notes, tables = self.fixture()
        tables["training_baselines.csv"][0].update(roi_mse=0, roi_psnr="inf")
        tables["training_evaluations.csv"][0].update(baseline_roi_mse=0, roi_mse=.65025,
                                                     roi_psnr=50, roi_drop_score=50)
        tables["training_ranking.csv"][0]["mean_roi_drop_score"] = 50/3
        self.assertEqual(self.validate(notes, tables)["selected"]["hardware_id"], "h1")

    def test_original_output_monitor_domain_remains_legal(self):
        notes, tables = self.fixture()
        for name in ("candidate_hardware.csv", "training_configurations.csv",
                     "frozen_hardware.csv", "frozen_seeds.csv"):
            for row in tables[name]:
                if row["node_id"] == 0:
                    row.update(monitor="baseline_output", bit=11)
                    if "lower" in row: row.update(lower=-4096, upper=4095)
        self.assertEqual(self.validate(notes, tables)["selected"]["key"][0],
                         (0, "baseline_output", 11))

    def test_psnr_forgery_unknown_status_and_failed_run_are_rejected(self):
        for fault in ("psnr", "status", "completed", "missing_table", "duplicate_run_key"):
            with self.subTest(fault=fault):
                notes, tables = self.fixture()
                if fault == "psnr": tables["training_evaluations.csv"][0]["global_psnr"] = 40
                elif fault == "status": tables["training_evaluations.csv"][0]["status"] = "run_failed"
                elif fault == "completed": notes["completed"] = "0"
                elif fault == "missing_table": del tables["candidate_hardware.csv"]
                else: notes["extra"] = "value\ncompleted=1"
                with self.assertRaises(ValueError): self.validate(notes, tables)


if __name__ == "__main__": unittest.main()
