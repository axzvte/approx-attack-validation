"""Fixture runner checks; real Yosys is used only for a tee/help path probe."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
KINDS = ("attack_comb", "config_storage", "attack_complete", "host_baseline", "host_attacked")


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec("run_bitflip_area"), "runner is not implemented")
        import run_bitflip_area
        self.runner = run_bitflip_area
        self.tmp = tempfile.TemporaryDirectory(prefix=".area-runner-test-", dir=SCRIPTS.parent)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.library_path = self.root / "library.json"
        self.library_path.write_text("{}")
        self.liberty = self.root / "typical.lib"
        self.liberty.write_text("fixture")
        self.library = {"liberty_path": str(self.liberty), "area_unit": "um^2",
                        "cell_areas": {"AND2_X1": 1.0, "DFF_X1": 2.0},
                        "sequential_cells": ["DFF_X1"],
                        "lef_verified_cells": ["AND2_X1", "DFF_X1"]}

    def generate(self, output, config_path=None):
        output.mkdir()
        (output / "rtl").mkdir()
        (output / "rtl" / "fixture.sv").write_text("module fixture; endmodule\n")
        manifest = {"schema": "bitflip_area_design_v1", "scope": "selected_unit_bank_not_full_application",
                    "rtl_files": ["rtl/fixture.sv"], "applications": [
                        {"application": name, "config_bits": bits, "nodes": 4 if name == "sharpen" else 6,
                         "tops": {kind: name + "_" + kind for kind in KINDS}}
                        for name, bits in [("sharpen", 96), ("conv", 148), ("sobel", 148), ("dct", 148)]]}
        (output / "design_manifest.json").write_text(json.dumps(manifest))
        return manifest

    def invoke(self, output=None, dry_run=False, process=None):
        output = output or self.root / "result"
        with patch.object(self.runner, "load_library_manifest", return_value=self.library), \
             patch.object(self.runner, "generate_design", side_effect=self.generate), \
             patch.object(self.runner.subprocess, "run", side_effect=process or AssertionError("No processes allowed")):
            return self.runner.run_area(self.library_path, output, dry_run=dry_run)

    def fake_process(self, args, **kwargs):
        if args[-1] == "-V":
            kwargs["stdout"].write("Yosys fixture version\n")
            return subprocess.CompletedProcess(args, 0)
        script = Path(args[-1])
        top = script.stem
        kind = next(k for k in KINDS if top.endswith("_" + k))
        bits = 96 if top.startswith("sharpen_") else 148
        cells = {"gate": {"type": "AND2_X1", "connections": {"Y": [1]}, "port_directions": {"Y": "output"}}}
        if kind in ("config_storage", "attack_complete", "host_attacked"):
            cells.update({f"q{i}": {"type": "DFF_X1", "connections": {"Q": [i+2]},
                                   "port_directions": {"Q": "output"}} for i in range(bits)})
        counts = {"AND2_X1": 1}
        if len(cells) > 1:
            counts["DFF_X1"] = bits
        area = sum(self.library["cell_areas"][k] * v for k, v in counts.items())
        stat = {"modules": {"\\" + top: {"num_cells": len(cells), "num_cells_by_type": counts, "area": area}},
                "design": {"num_cells": len(cells), "num_cells_by_type": counts, "area": area}}
        target = Path(kwargs["cwd"]) / "results" / top
        (target / "stat.json").write_text(json.dumps(stat))
        (target / "mapped.json").write_text(json.dumps({"modules": {top: {"cells": cells}}}))
        (target / "mapped.v").write_text("// fixture, not synthesis\n")
        kwargs["stdout"].write("fixture process output\n")
        return subprocess.CompletedProcess(args, 0)

    def test_dry_run_makes_twenty_scripts_without_process(self):
        result = self.invoke(dry_run=True)
        self.assertEqual(result["state"], "dry_run")
        scripts = list((self.root / "result" / "scripts").glob("*.ys"))
        self.assertEqual(len(scripts), 20)
        for script in scripts:
            text = script.read_text()
            for command in ("synth -flatten -noabc", "dffunmap", "dfflibmap -liberty", "abc -liberty", "check -assert", "tee -o", "stat -json"):
                self.assertIn(command, text)
        self.assertFalse((self.root / "result" / "summary.csv").exists())

    @unittest.skipUnless(shutil.which('yosys'), 'Yosys required for report redirection check only')
    def test_generated_report_redirection_works_in_output_path_with_spaces(self):
        output=self.root/'output with spaces'
        self.invoke(output=output,dry_run=True)
        script=output/'scripts/sharpen_attack_comb.ys'
        tee=next(line for line in script.read_text().splitlines() if line.startswith('tee -o '))
        prefix,separator,_=tee.partition(' stat -json ')
        self.assertTrue(separator)
        # Exercise the real tee file handling without loading RTL, a library,
        # or any synthesis pass. Reusing the generated prefix catches quoting.
        probe=output/'report_path_probe.ys'
        probe.write_text(prefix+' help stat\n')
        result=subprocess.run(['yosys','-Q','-T','-s',str(probe)],cwd=output,
            stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=False)
        self.assertEqual(result.returncode,0,result.stdout[-2000:])
        report=output/'results/sharpen_attack_comb/stat.json'
        self.assertTrue(report.is_file())
        self.assertIn('stat',report.read_text())

    def test_existing_output_is_untouched(self):
        output = self.root / "existing"
        output.mkdir()
        with self.assertRaises(ValueError):
            self.invoke(output, dry_run=True)
        self.assertEqual(list(output.iterdir()), [])

    def test_overlap_with_library_is_rejected(self):
        with self.assertRaises(ValueError):
            self.invoke(self.root / "library.json" / "nested", dry_run=True)

    def test_fixture_success_writes_scoped_summary(self):
        result = self.invoke(process=self.fake_process)
        self.assertEqual(result["state"], "complete")
        self.assertEqual(len(result["jobs"]), 20)
        summary = (self.root / "result" / "summary.md").read_text()
        self.assertIn("selected arithmetic-unit bank", summary)
        self.assertIn("not additive", summary)
        self.assertIn("um^2", summary)
        measurement=next(j['measurement'] for j in result['jobs'] if j['top']=='sharpen_attack_complete')
        self.assertIn('sequential_area_um2',measurement)
        self.assertEqual(measurement['sequential_area_um2'],192.0)
        self.assertEqual(measurement['combinational_area_um2'],1.0)
        self.assertTrue(result["source_sha256"])

    def test_failed_process_preserves_log_and_failed_state(self):
        def fail(args, **kwargs):
            kwargs["stdout"].write("fixture failure\n")
            return subprocess.CompletedProcess(args, 1)
        with self.assertRaises(RuntimeError):
            self.invoke(process=fail)
        manifest = json.loads((self.root / "result" / "run_manifest.json").read_text())
        self.assertEqual(manifest["state"], "failed")
        self.assertIn("fixture failure", (self.root / "result" / "yosys_version.log").read_text())
        self.assertFalse((self.root / "result" / "summary.csv").exists())

    def test_failed_synthesis_preserves_completed_jobs(self):
        def fail_second(args, **kwargs):
            if args[-1].endswith("sharpen_config_storage.ys"):
                kwargs["stdout"].write("fixture synthesis failure\n")
                return subprocess.CompletedProcess(args, 7)
            return self.fake_process(args, **kwargs)
        with self.assertRaises(RuntimeError):
            self.invoke(process=fail_second)
        output = self.root / "result"
        manifest = json.loads((output / "run_manifest.json").read_text())
        self.assertEqual(manifest["state"], "failed")
        self.assertEqual(manifest["jobs"][0]["state"], "complete")
        self.assertEqual(manifest["jobs"][1]["state"], "failed")
        self.assertEqual(manifest["jobs"][1]["returncode"], 7)
        self.assertTrue((output / "results/sharpen_attack_comb/stat.json").is_file())
        self.assertFalse((output / "summary.csv").exists())

    def test_missing_artifact_fails_even_when_process_exits_zero(self):
        def missing(args, **kwargs):
            if args[-1] == "-V":
                return self.fake_process(args, **kwargs)
            kwargs["stdout"].write("fixture missing outputs\n")
            return subprocess.CompletedProcess(args, 0)
        with self.assertRaisesRegex(ValueError, "Missing or empty"):
            self.invoke(process=missing)
        manifest = json.loads((self.root / "result/run_manifest.json").read_text())
        self.assertEqual(manifest["state"], "failed")

    def test_alternate_frozen_structure_retains_generated_bit_count(self):
        original = self.generate
        def alternate(output, config_path=None):
            design = original(output, config_path)
            design["applications"][0].update(nodes=1, monitor_bits=13, config_bits=26)
            (output / "design_manifest.json").write_text(json.dumps(design))
            return design
        self.generate = alternate
        result = self.invoke(dry_run=True)
        jobs = [j for j in result["jobs"] if j["application"] == "sharpen"]
        self.assertEqual([j["expected_config_bits"] for j in jobs], [0, 26, 26, 0, 26])

    def test_inconsistent_generated_counts_rejected(self):
        original = self.generate
        for i, changes in enumerate([{"config_bits": 0}, {"config_bits": True},
                {"monitor_bits": 48, "config_bits": 98}, {"nodes": 1, "monitor_bits": 48} ]):
            def invalid(output, config_path=None):
                design = original(output, config_path)
                design["applications"][0].update(changes)
                return design
            self.generate = invalid
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.invoke(self.root / f"invalid-{i}", dry_run=True)

    def test_lef_and_license_are_fingerprinted(self):
        for key in ("lef_path", "license_path"):
            path = self.root / key
            path.write_text("fixture verification input")
            self.library[key] = str(path)
        result = self.invoke(dry_run=True)
        for key in ("lef_path", "license_path"):
            self.assertIn(self.library[key], result["source_sha256"])

    def test_source_drift_before_first_job_prevents_synthesis(self):
        called = []
        def drift(args, **kwargs):
            called.append(args)
            result = self.fake_process(args, **kwargs)
            self.liberty.write_text("changed after version query")
            return result
        with self.assertRaisesRegex(ValueError, "Source changed"):
            self.invoke(process=drift)
        self.assertEqual(len(called), 1)
        state = json.loads((self.root / "result/run_manifest.json").read_text())
        self.assertEqual(state["state"], "failed")
        self.assertFalse((self.root / "result/summary.csv").exists())

    def test_source_drift_during_last_job_prevents_summary(self):
        def drift(args, **kwargs):
            result = self.fake_process(args, **kwargs)
            if args[-1].endswith("dct_host_attacked.ys"):
                (Path(kwargs["cwd"]) / "design/rtl/fixture.sv").write_text("changed during last job")
            return result
        with self.assertRaisesRegex(ValueError, "Source changed"):
            self.invoke(process=drift)
        self.assertFalse((self.root / "result/summary.csv").exists())
        state = json.loads((self.root / "result/run_manifest.json").read_text())
        self.assertEqual(state["state"], "failed")

    def data(self):
        return ({"modules": {"\\top": {"area": 1.0, "num_cells": 1, "num_cells_by_type": {"AND2_X1": 1}}}},
                {"modules": {"top": {"cells": {"a": {"type": "AND2_X1"}}}}})

    def test_real_flat_stat_schema(self):
        stat, mapped = self.data()
        result = self.runner.validate_result(stat, mapped, "top", self.library, 0)
        self.assertEqual(result["area_um2"], 1.0)

    def test_legacy_mode_uses_historical_recipe_scope_and_reproduction_check(self):
        def legacy(output,config_path=None):
            m=self.generate(output,config_path)
            m['scope']='single_arithmetic_kernel_not_full_image_system'
            m['liberty_sha256']=self.runner._sha(self.liberty)
            for a in m['applications']:
                a['historical_baseline_um2']=1.0
            return m
        output=self.root/'legacy_result'
        with patch.object(self.runner,'load_library_manifest',return_value=self.library), \
             patch.object(self.runner,'generate_legacy_design',side_effect=legacy), \
             patch.object(self.runner.subprocess,'run',side_effect=self.fake_process):
            m=self.runner.run_area(self.library_path,output,legacy_kernel=True)
        self.assertEqual(m['state'],'complete')
        self.assertEqual(m['scope'],'single_arithmetic_kernel_not_full_image_system')
        self.assertEqual(m['recipe'],'legacy_runtime')
        script=(output/'scripts/sharpen_host_attacked.ys').read_text()
        self.assertNotIn('\ndffunmap\n',script)
        self.assertLess(script.index('abc -liberty'),script.index('read_liberty -lib'))
        with (output/'summary.csv').open() as handle:
            row=next(__import__('csv').DictReader(handle))
        self.assertIn('kernel_overhead_percent',row)
        self.assertNotIn('selected_bank_overhead_percent',row)
        self.assertEqual(float(row['historical_baseline_delta_um2']),0)
        self.assertEqual(int(row['configured_bits']),96)
        self.assertEqual(int(row['retained_config_bits']),96)

    def test_legacy_library_mismatch_fails_before_tools(self):
        def legacy(output,config_path=None):
            m=self.generate(output,config_path)
            m['scope']='single_arithmetic_kernel_not_full_image_system'
            m['liberty_sha256']='0'*64
            return m
        with patch.object(self.runner,'load_library_manifest',return_value=self.library), \
             patch.object(self.runner,'generate_legacy_design',side_effect=legacy), \
             patch.object(self.runner.subprocess,'run') as process:
            with self.assertRaisesRegex(ValueError,'historical Liberty'):
                self.runner.run_area(self.library_path,self.root/'legacy_result',legacy_kernel=True)
            process.assert_not_called()

    def test_legacy_integration_records_pruned_config_bits_without_hiding_them(self):
        stat,mapped=self.data()
        result=self.runner.validate_result(stat,mapped,'top',self.library,96,allow_pruned_config=True)
        self.assertEqual(result['sequential_bits'],0)
        self.assertEqual(result['pruned_config_bits'],96)
        with self.assertRaises(ValueError):
            self.runner.validate_result(stat,mapped,'top',self.library,96)

    def test_invalid_areas_rejected(self):
        for area in [None, 0, -1, float("nan"), float("inf"), True]:
            stat, mapped = self.data()
            stat["modules"]["\\top"]["area"] = area
            with self.subTest(area=area), self.assertRaises(ValueError):
                self.runner.validate_result(stat, mapped, "top", self.library, 0)

    def test_unknown_unmapped_unverified_cells_rejected(self):
        for name in ["$add", "$_DFF_P_", "UNKNOWN", "UNVERIFIED"]:
            stat, mapped = self.data()
            mapped["modules"]["top"]["cells"]["a"]["type"] = name
            lib = copy.deepcopy(self.library)
            lib["cell_areas"]["UNVERIFIED"] = 1
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.runner.validate_result(stat, mapped, "top", lib, 0)

    def test_count_and_area_disagreement_rejected(self):
        for field, value in [("num_cells", 2), ("num_cells_by_type", {"AND2_X1": 2}), ("area", 3)]:
            stat, mapped = self.data()
            stat["modules"]["\\top"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.runner.validate_result(stat, mapped, "top", self.library, 0)

    def test_missing_config_bits_rejected(self):
        stat, mapped = self.data()
        with self.assertRaisesRegex(ValueError, "sequential"):
            self.runner.validate_result(stat, mapped, "top", self.library, 96)

    def test_multibit_sequential_cell_rejected(self):
        stat, mapped = self.data()
        stat["modules"]["\\top"].update(area=2, num_cells_by_type={"DFF_X1": 1})
        mapped["modules"]["top"]["cells"]["a"] = {"type": "DFF_X1", "connections": {"Q": [1, 2]}, "port_directions": {"Q": "output"}}
        with self.assertRaises(ValueError):
            self.runner.validate_result(stat, mapped, "top", self.library, 1)

    def test_hidden_unmapped_memory_rejected(self):
        stat, mapped = self.data()
        mapped["modules"]["top"]["memories"] = {"unmapped": {"width": 8, "size": 2}}
        with self.assertRaisesRegex(ValueError, "memory"):
            self.runner.validate_result(stat, mapped, "top", self.library, 0)


if __name__ == "__main__":
    unittest.main()
