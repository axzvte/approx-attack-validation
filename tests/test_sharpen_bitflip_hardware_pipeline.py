"""User-run pipeline tests. Real orchestration, controlled external solver only."""
import csv
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

script=Path(__file__).resolve().parents[1]/"scripts/run_sharpen_bitflip_hardware.py"
sys.path.insert(0,str(script.parent))
spec=importlib.util.spec_from_file_location("hardware_pipeline",script)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

def table(path,fields,rows):
    with path.open("w",newline="") as f:
        w=csv.DictWriter(f,fields);w.writeheader();w.writerows(rows)
def read(path):
    with path.open(newline="") as f:return list(csv.DictReader(f))

class PipelineTests(unittest.TestCase):
    def fixture(self,root):
        data=root/"data";(data/"mask").mkdir(parents=True);(data/"mask/roi_mask.jpg").write_bytes(b"fixture")
        source=root/"training";source.mkdir()
        summaries=[];configs=[]
        for image in (1,2):
            summaries.append(dict(image=image,candidate_id="baseline",status="reference",roi_mse=20,global_mse=20))
            for rank,bit in ((1,1),(2,2)):
                summaries.append(dict(image=image,candidate_id=f"n2_rank{rank}",status="numeric_eligible",roi_mse=200+rank,global_mse=60))
                for node in (0,1):
                    configs.append(dict(image=image,candidate_id=f"n2_rank{rank}",node_id=node,node_name=f"A{node+1}",monitor="input1",bit=bit,lower=image*10,upper=image*10+rank))
        table(source/"summary_all.csv",["image","candidate_id","status","roi_mse","global_mse"],summaries)
        table(source/"configurations_all.csv",["image","candidate_id","node_id","node_name","monitor","bit","lower","upper"],configs)
        exe=root/"fixed_solver"
        exe.write_text('''#!/usr/bin/env python3
import csv
from pathlib import Path
import sys
out=Path(sys.argv[2]);opts=dict(zip(sys.argv[3::2],sys.argv[4::2]));image=int(opts["--image"])
hardware=Path(opts["--hardware"])
with hardware.open() as f:h=list(csv.DictReader(f))
bit=int(h[0]["bit"])
print("image",image,"hardware",str(hardware),"seeds",opts.get("--seeds"),flush=True)
if (Path(__file__).parent/"fail_screen").exists() and image==3:sys.exit(7)
out.mkdir()
def table(name,fields,rows):
 with (out/name).open("w",newline="") as f:
  w=csv.DictWriter(f,fields);w.writeheader();w.writerows(rows)
# Bit2 is perfect on image3 but fails on4; bit1 succeeds on both and must win.
ok=not(bit==2 and image==4)
summary=[dict(candidate_id="baseline",status="reference",installed_nodes=0,triggered_nodes=0,visual_status="reference",global_mse=20,roi_mse=20,non_roi_mse=20,search_status="budget_exhausted",calibration_seconds=.1),
 dict(candidate_id="selected",status="numeric_eligible" if ok else "quality_not_met",installed_nodes=len(h),triggered_nodes=1,visual_status="pending_user_review",global_mse=60 if ok else 80,roi_mse=200 if bit==1 else 2000,non_roi_mse=40,search_status="budget_exhausted",calibration_seconds=.1)]
table("summary.csv",list(summary[0]),summary)
cs=[dict(candidate_id="selected",node_id=r["node_id"],monitor=r["monitor"],bit=r["bit"],lower=image,upper=image+1,dynamic_triggers=5 if i==0 else 0) for i,r in enumerate(h)]
table("configurations.csv",list(cs[0]),cs)
table("search_stats.csv",["contexts_built","context_budget_exhausted"],[dict(contexts_built=2,context_budget_exhausted=1)])
images=[]
if ok and opts["--image-mode"]!="none":
 for kind in ("output","roi_comparison","background_comparison"):
  name="selected_"+kind+".png";(out/name).write_bytes(b"fixture image")
  images.append(dict(candidate_id="selected",image_kind=kind,path=name))
table("images.csv",["candidate_id","image_kind","path"],images)
(out/"RUN.txt").write_text("completed=0\\nimage="+str(image)+"\\ncompleted=1\\n")
''',encoding="utf-8");exe.chmod(0o755)
        return data,source,exe
    def args(self,data,source,exe,out):
        return [str(data),str(out),"--training-report",str(source),"--train","1:2","--screen","3:4","--test","5:6","--skip-build","--fixed-exe",str(exe),"--max-hardware","0"]
    def test_structure_dedup_ranking_and_test_bank_frozen(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root);out=root/"run"
            (source/"RUN.json").write_text(json.dumps(dict(completed=True,all_succeeded=True)))
            self.assertEqual(module.main(self.args(data,source,exe,out)),0)
            pool=read(out/"candidate_pool.csv");self.assertEqual(len(pool),2)
            self.assertTrue(all(int(r["source_count"])==2 for r in pool))
            ranking=read(out/"screening_ranking.csv");self.assertEqual(int(ranking[0]["numeric_successes"]),2)
            self.assertEqual({r["bit"] for r in read(out/"frozen_hardware.csv")},{"1"})
            best=read(out/"best_per_image.csv");self.assertEqual([int(r["image"]) for r in best],[5,6])
            self.assertTrue(all(r["status"]=="numeric_eligible" for r in best))
            configs=read(out/"configurations_all.csv");self.assertEqual(len(configs),4)
            self.assertTrue(any(int(r["dynamic_triggers"])==0 for r in configs))
            self.assertEqual(len(read(out/"images_all.csv")),6)
            self.assertFalse((out/"ablations_all.csv").exists())
            run=json.loads((out/"RUN.json").read_text());self.assertTrue(run["completed"])
            self.assertEqual(run["selection_status"],"selected")
            self.assertTrue(run["seed_bank_unchanged_after_test"])
            self.assertEqual((out/"training_source/summary_all.csv").read_bytes(),(source/"summary_all.csv").read_bytes())
            self.assertEqual((out/"training_source/configurations_all.csv").read_bytes(),(source/"configurations_all.csv").read_bytes())
            self.assertEqual((out/"training_source/RUN.json").read_bytes(),(source/"RUN.json").read_bytes())
            frozen=read(out/"frozen_seeds.csv")
            self.assertTrue(all(r["seed_id"].startswith(("train_01_","train_02_")) for r in frozen))
            self.assertEqual({int(r["lower"]) for r in frozen},{10,20})
            for image in (5,6):
                self.assertIn(str(out/"frozen_seeds.csv"),(out/f"logs/test_{run['selected_hardware_id']}_image_{image:02d}.log").read_text())
            original=(out/"frozen_hardware.csv").read_bytes()
            self.assertEqual(module.main(self.args(data,source,exe,out)),1)
            self.assertEqual(original,(out/"frozen_hardware.csv").read_bytes())
    def test_split_overlap_and_import_leak_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root)
            bad=self.args(data,source,exe,root/"bad");bad[bad.index("--screen")+1]="2:4"
            self.assertEqual(module.main(bad),1);self.assertFalse((root/"bad").exists())
            summaries=read(source/"summary_all.csv");summaries[0]["image"]="5"
            table(source/"summary_all.csv",list(summaries[0]),summaries)
            self.assertEqual(module.main(self.args(data,source,exe,root/"leak")),1)
            self.assertFalse((root/"leak/frozen_hardware.csv").exists())
    def test_screening_failure_continues_but_blocks_freeze(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root);(root/"fail_screen").write_text("simulate external failure")
            out=root/"failed"
            self.assertEqual(module.main(self.args(data,source,exe,out)),1)
            results=read(out/"screening_results.csv")
            self.assertEqual(len(results),4)
            self.assertEqual(sum(r["status"]=="run_failed" for r in results),2)
            self.assertFalse((out/"frozen_hardware.csv").exists())
            self.assertFalse(read(out/"best_per_image.csv"))
    def test_unknown_source_status_and_non_candidate_configuration_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root)
            ss=read(source/"summary_all.csv");original=[dict(r) for r in ss]
            ss[1]["status"]="report_failed";table(source/"summary_all.csv",list(ss[0]),ss)
            with self.assertRaises(ValueError):module.training_pool(source,[1,2],30)
            table(source/"summary_all.csv",list(original[0]),original)
            cs=read(source/"configurations_all.csv");cs[0]["candidate_id"]="baseline"
            table(source/"configurations_all.csv",list(cs[0]),cs)
            with self.assertRaises(ValueError):module.training_pool(source,[1,2],30)
    def test_round_robin_shortlist_covers_different_training_images(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root)
            cs=read(source/"configurations_all.csv")
            for r in cs:
                if r["image"]=="2":r["bit"]=str(int(r["bit"])+2)
            table(source/"configurations_all.csv",list(cs[0]),cs)
            pool,order=module.training_pool(source,[1,2],30)
            self.assertEqual(len(pool),4)
            self.assertEqual(order[0][0][2],2)
            self.assertEqual(order[1][0][2],4)
    def test_incomplete_or_failed_training_metadata_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root)
            for completed,succeeded in ((False,True),(True,False)):
                with self.subTest(completed=completed,all_succeeded=succeeded):
                    (source/"RUN.json").write_text(json.dumps(dict(completed=completed,all_succeeded=succeeded)))
                    out=root/f"rejected_{completed}_{succeeded}"
                    self.assertEqual(module.main(self.args(data,source,exe,out)),1)
                    self.assertFalse(out.exists())
    def test_no_legal_state_preserves_missing_metrics_and_zero_score(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root);out=root/"reports"
            self.assertEqual(module.main(self.args(data,source,exe,out)),0)
            run=json.loads((out/"RUN.json").read_text());h=module.hardware_key(read(out/"frozen_hardware.csv"))
            relative=f"test/{run['selected_hardware_id']}/image_05";report=out/relative
            summaries=read(report/"summary.csv")
            for r in summaries:
                if r["candidate_id"]=="selected":
                    r.update(status="no_legal_interval_state",global_mse="",roi_mse="",non_roi_mse="",triggered_nodes="",visual_status="not_available")
            table(report/"summary.csv",list(summaries[0]),summaries)
            table(report/"configurations.csv",["candidate_id","node_id","monitor","bit"],[])
            table(report/"images.csv",["candidate_id","image_kind","path"],[])
            staged,row=module.fixed_report(report,5,h,"compact",relative,30)
            self.assertEqual(row["status"],"no_legal_interval_state")
            self.assertEqual(row["roi_drop_score"],0)
            self.assertEqual(row["roi_mse"],"")
            self.assertFalse(staged["configurations_all.csv"])
    def test_fixed_report_rejects_hardware_change_and_escaping_image_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root);out=root/"reports"
            self.assertEqual(module.main(self.args(data,source,exe,out)),0)
            run=json.loads((out/"RUN.json").read_text());h=module.hardware_key(read(out/"frozen_hardware.csv"))
            relative=f"test/{run['selected_hardware_id']}/image_05";report=out/relative
            configs=read(report/"configurations.csv");original=[dict(r) for r in configs];configs[0]["bit"]="9"
            table(report/"configurations.csv",list(configs[0]),configs)
            with self.assertRaises(ValueError):module.fixed_report(report,5,h,"compact",relative,30)
            table(report/"configurations.csv",list(original[0]),original)
            image_rows=read(report/"images.csv");image_rows[0]["path"]="../../outside.png"
            table(report/"images.csv",list(image_rows[0]),image_rows)
            with self.assertRaises(ValueError):module.fixed_report(report,5,h,"compact",relative,30)
    def test_test_seed_mutation_stops_before_next_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root)
            script_text=exe.read_text()
            exe.write_text(script_text+'\nif image==5:Path(opts["--seeds"]).write_text("tampered frozen seed bank")\n')
            out=root/"mutated"
            self.assertEqual(module.main(self.args(data,source,exe,out)),1)
            self.assertEqual([int(r["image"]) for r in read(out/"best_per_image.csv")],[5])
            run=json.loads((out/"RUN.json").read_text())
            self.assertFalse(run["completed"])
            self.assertFalse((out/f"logs/test_{run['selected_hardware_id']}_image_06.log").exists())
if __name__=="__main__":unittest.main()
