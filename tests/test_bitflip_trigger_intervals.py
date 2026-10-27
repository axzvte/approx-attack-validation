"""Hand-calculated interval fixtures. Execution belongs to the user."""
import csv
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/search_bitflip_intervals.py"
spec = importlib.util.spec_from_file_location("bitflip_intervals", SCRIPT)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


class IntervalTests(unittest.TestCase):
    def setUp(self):
        self.image = module.ImageStats(2, 10, 2, 10)
        self.bins = {10: module.Bin(8, -1, 0, 1, 1),
                     20: module.Bin(-1, 20, 0, 1, 1)}

    def test_interval_beats_full_range_and_selects_compensation(self):
        result = module.search_intervals(self.image, self.bins, 0, 30)
        a, b, full = (result[k] for k in ("roi_amplify", "background_compensate", "full"))
        self.assertEqual((a["lower"], a["upper"]), (10, 10))
        self.assertEqual((b["lower"], b["upper"]), (10, 10))
        self.assertEqual(a["roi_mse"], 5)
        self.assertEqual(full["roi_mse"], 4.5)
        self.assertEqual(b["non_roi_mse"], 0.9)
        self.assertEqual(full["non_roi_mse"], 2.9)
        # Denominators include unchanged pixels, not just triggered samples.
        self.assertEqual(a["roi_trigger_rate"], 0.5)
        self.assertEqual(a["non_roi_trigger_rate"], 0.1)

    def test_reject_invalid_interval_and_do_not_substitute_baseline(self):
        result = module.search_intervals(self.image, {10: module.Bin(8, -1, 1, 1, 1)}, 0, 30)
        self.assertEqual(result["full"]["status"], "range_invalid")
        self.assertEqual(result["roi_amplify"]["status"], "no_feasible_interval")
        self.assertEqual(result["roi_amplify"]["roi_mse"], "")
        self.assertEqual(result["background_compensate"]["has_interval"], 0)
        self.assertEqual(result["range_rejected"], 1)

    def test_quality_constraint_and_no_compensation(self):
        result = module.search_intervals(self.image, self.bins, 0, 60)
        self.assertEqual(result["roi_amplify"]["has_interval"], 0)
        positive = module.search_intervals(self.image, {1: module.Bin(5, 1, 0, 1, 1)}, 0, 30)
        self.assertEqual(positive["background_compensate"]["has_interval"], 0)

    def test_reduced_grid_keeps_every_single_value(self):
        bins = {1: module.Bin(-1, 2, 0, 1, 1),
                2: module.Bin(8, -1, 0, 1, 1),
                3: module.Bin(-1, 2, 0, 0, 0)}
        result = module.search_intervals(self.image, bins, 2, 30)
        self.assertEqual((result["roi_amplify"]["lower"],result["roi_amplify"]["upper"]), (2,2))
        self.assertEqual(result["evaluated"], 4)  # three singletons and full range

    def test_empty_region_and_bad_parameters_rejected(self):
        for budget, threshold in [(1,30),(-1,30),(0,float("nan"))]:
            with self.assertRaises(ValueError):
                module.search_intervals(self.image,self.bins,budget,threshold)
        with self.assertRaises(ValueError):
            module.search_intervals(module.ImageStats(0,10,0,10),self.bins,0,30)

    def write_profiles(self, root):
        root.mkdir()
        (root / "RUN.txt").write_text("completed=0\nschema_version=1\nbaseline=all_5RP\napplication=Sharpening\ncompleted=1\n")
        def write(name, fields, data):
            with (root/name).open("w",newline="") as f:
                writer=csv.DictWriter(f,fields);writer.writeheader();writer.writerows(data)
        write("image_summary.csv",["image","roi_pixels","non_roi_pixels","roi_sse","non_roi_sse"],
              [dict(image=1,roi_pixels=2,non_roi_pixels=10,roi_sse=2,non_roi_sse=10)])
        summary,groups=[],[]
        for node in range(4):
            for bit in range(12):
                for region in ("roi","non_roi"):
                    common=dict(image=1,node_id=node,bit=bit,region=region,samples=1,
                                valid_final_samples=1,invalid_downstream=0,final_delta_sse=0,
                                baseline_final_sse_valid=1)
                    summary.append(common)
                    for monitor in ("input1","input2","baseline_output"):
                        groups.append(dict(common,monitor=monitor,monitor_value=10))
        write("bit_summary.csv",list(summary[0]),summary)
        write("monitor_groups.csv",list(groups[0]),groups)

    def test_completed_profiles_cli_protection_and_input_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data=root/"profiles";out=root/"intervals"
            self.write_profiles(data)
            self.assertEqual(module.main([str(data),str(out),"--boundary-budget","0"]),0)
            with (out/"comparisons.csv").open() as f:
                rows=list(csv.DictReader(f))
            self.assertEqual(len(rows),144*4)
            self.assertEqual(module.main([str(data),str(out)]),1)
            self.assertIn('"completed": true',(out/"RUN.json").read_text())
            # Duplicate bins must fail, not double their apparent benefit.
            path=data/"monitor_groups.csv"
            original=path.read_text();path.write_text(original+original.splitlines()[1]+"\n")
            self.assertEqual(module.main([str(data),str(root/"duplicate")]),1)
            path.write_text(original)
            (data/"RUN.txt").write_text("completed=0\n")
            self.assertEqual(module.main([str(data),str(root/"incomplete")]),1)


if __name__ == "__main__":
    unittest.main()
