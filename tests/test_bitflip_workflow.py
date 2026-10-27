"""User-run tests for safe screening bounds and read-only task reuse.

The authoring agent does not execute this file or syntax checks.
"""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "bitflip_workflow.py"
SPEC = importlib.util.spec_from_file_location("bitflip_workflow_helpers", SCRIPT)
workflow = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(workflow)


class EliminationTests(unittest.TestCase):
    def setUp(self):
        self.hardware = {"hardware_id": "h0002", "key": ((0, "input1", 3), (1, "input2", 4))}
        self.incumbent = dict(hardware_id="h0001", complete=1, evaluated_images=10,
                              screen_images=10, numeric_successes=10, worst_roi_drop_score=9.47)

    def row(self, image, score=8.55, status="numeric_eligible"):
        return dict(hardware_id="h0002", image=image, status=status, roi_drop_score=score)

    def test_strict_worst_bound_eliminates_against_complete_incumbent(self):
        reason = workflow.elimination_reason(self.hardware, [self.row(11)], self.incumbent, 10)
        self.assertIn("worst", reason)

    def test_success_upper_bound_is_primary(self):
        incumbent = {**self.incumbent, "numeric_successes": 8, "worst_roi_drop_score": 0}
        rows = [self.row(11, 0, "quality_not_met"), self.row(12, 0, "quality_not_met"),
                self.row(13, 0, "quality_not_met")]
        self.assertIn("success", workflow.elimination_reason(self.hardware, rows, incumbent, 10))
        # A candidate that can still achieve more successes must not be discarded
        # because its observed worst image is weaker.
        incumbent = {**self.incumbent, "numeric_successes": 9}
        self.assertEqual(workflow.elimination_reason(self.hardware, [self.row(11, 1)], incumbent, 10), "")

    def test_equal_worst_is_not_a_bound_on_final_mean(self):
        self.assertEqual(workflow.elimination_reason(self.hardware, [self.row(11, 9.47)], self.incumbent, 10), "")

    def test_incomplete_incumbent_never_prunes(self):
        for change in ({"complete": 0}, {"evaluated_images": 9}, {"screen_images": 9}):
            self.assertEqual(workflow.elimination_reason(self.hardware, [self.row(11)],
                                                        {**self.incumbent, **change}, 10), "")

    def test_failed_duplicate_foreign_or_invalid_rows_never_prune(self):
        examples = [
            [self.row(11, 0, "run_failed")],
            [self.row(11), self.row(11)],
            [{**self.row(11), "hardware_id": "h9999"}],
            [self.row(11, float("nan"))],
            [self.row(11, float("inf"))],
            [self.row(11, -1)],
            [self.row(11, 0, "bound_eliminated")],
        ]
        for rows in examples:
            self.assertEqual(workflow.elimination_reason(self.hardware, rows, self.incumbent, 10), "")

    def test_full_candidate_and_empty_rows_are_not_pruning_work(self):
        self.assertEqual(workflow.elimination_reason(self.hardware, [], self.incumbent, 10), "")
        rows = [self.row(i) for i in range(10)]
        self.assertEqual(workflow.elimination_reason(self.hardware, rows, self.incumbent, 10), "")


class TaskCacheTests(unittest.TestCase):
    def fixture(self, root):
        source = root / "source"
        task = source / "screen/h0001/image_11"
        task.mkdir(parents=True)
        (task / "summary.csv").write_text("candidate_id,status\nselected,numeric_eligible\n", encoding="utf-8")
        (task / "RUN.txt").write_text("completed=1\n", encoding="utf-8")
        signature = dict(application="conv", image_sha="image-a", roi_sha="roi-a", code_sha="code-a",
                         executable_sha="exe-a", hardware_sha="H-a", seeds_sha="seeds-a", options={"beam": 4})
        workflow.TaskCache(source).record("screen/h0001/image_11", signature)
        return source, task, signature

    def test_restore_copies_all_artifacts_without_mutating_source(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, task, signature = self.fixture(root)
            before = {p.relative_to(source).as_posix(): p.read_bytes() for p in source.rglob("*") if p.is_file()}
            destination = root / "destination"
            self.assertTrue(workflow.TaskCache(destination, source).restore("screen/h0001/image_11", signature))
            restored = destination / "screen/h0001/image_11"
            self.assertEqual((restored / "summary.csv").read_bytes(), (task / "summary.csv").read_bytes())
            after = {p.relative_to(source).as_posix(): p.read_bytes() for p in source.rglob("*") if p.is_file()}
            self.assertEqual(before, after)
            self.assertFalse(workflow.TaskCache(destination, source).restore("screen/h0001/image_11", signature))

    def test_each_signature_dimension_invalidates_reuse(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, _, signature = self.fixture(root)
            for index, name in enumerate(signature):
                changed = {**signature, name: {"beam": 8} if name == "options" else "changed"}
                destination = root / f"destination_{index}"
                self.assertFalse(workflow.TaskCache(destination, source).restore("screen/h0001/image_11", changed))
                self.assertFalse((destination / "screen/h0001/image_11").exists())

    def test_changed_missing_extra_and_invalid_manifest_artifacts_are_not_reused(self):
        for mutation in ("changed", "missing", "extra", "incomplete", "bad_manifest"):
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                source, task, signature = self.fixture(root)
                if mutation == "changed":
                    (task / "summary.csv").write_text("tampered", encoding="utf-8")
                elif mutation == "missing":
                    (task / "summary.csv").unlink()
                elif mutation == "extra":
                    (task / "unrecorded.txt").write_text("extra", encoding="utf-8")
                elif mutation == "incomplete":
                    manifest = json.loads((task / "TASK.json").read_text(encoding="utf-8"))
                    manifest["completed"] = False
                    (task / "TASK.json").write_text(json.dumps(manifest), encoding="utf-8")
                else:
                    (task / "TASK.json").write_text("{", encoding="utf-8")
                destination = root / "destination"
                self.assertFalse(workflow.TaskCache(destination, source).restore("screen/h0001/image_11", signature))
                self.assertFalse((destination / "screen/h0001/image_11").exists())

    def test_unsafe_relative_paths_and_overlapping_roots_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, _, signature = self.fixture(root)
            cache = workflow.TaskCache(root / "destination", source)
            for relative in ("../outside", "/absolute", "C:/absolute", "screen/../outside", "", ".", "screen\\outside"):
                with self.assertRaises(ValueError):
                    cache.restore(relative, signature)
            for destination in (source, source / "nested", source.parent):
                with self.assertRaises(ValueError):
                    workflow.TaskCache(destination, source)

    def test_symlink_artifacts_are_rejected_without_reading_outside(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, task, signature = self.fixture(root)
            outside = root / "outside.txt"
            outside.write_text("private", encoding="utf-8")
            try:
                (task / "linked.txt").symlink_to(outside)
            except (OSError, NotImplementedError):
                self.skipTest("Symlink creation unavailable on this platform")
            with self.assertRaises(ValueError):
                workflow.TaskCache(root / "destination", source).restore("screen/h0001/image_11", signature)
            with self.assertRaises(ValueError):
                workflow.TaskCache(source).record("screen/h0001/image_11", signature)

    def test_source_fingerprint_tracks_code_and_excludes_data_results_tests(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "CMakeLists.txt").write_text("project(test)\n", encoding="utf-8")
            (root / "src").mkdir()
            code = root / "src/solver.cpp"
            code.write_text("int value=1;\n", encoding="utf-8")
            first = workflow.source_fingerprint(root)
            for name in ("data/input.jpg", "outputs/result.csv", "tests/test_example.py", "scripts/__pycache__/helper.pyc"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"ignored")
            self.assertEqual(workflow.source_fingerprint(root), first)
            code.write_text("int value=2;\n", encoding="utf-8")
            self.assertNotEqual(workflow.source_fingerprint(root), first)


if __name__ == "__main__":
    unittest.main()
