"""User-run tests of real batch orchestration against controlled external reports."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys

script=Path(__file__).resolve().parents[1]/"scripts/run_circuit_bitflip_batch.py"
sys.path.insert(0,str(script.parent))
spec=importlib.util.spec_from_file_location("circuit_batch",script)
batch=importlib.util.module_from_spec(spec);spec.loader.exec_module(batch)

class BatchTests(unittest.TestCase):
    def fixture(self,root):
        data=root/"data";(data/"mask").mkdir(parents=True);(data/"mask/roi_mask.jpg").write_bytes(b"fixture")
        exe=root/"fake_circuit"
        exe.write_text('''#!/usr/bin/env python3
import csv
from pathlib import Path
import sys
app=sys.argv[1];out=Path(sys.argv[3]);opts=dict(zip(sys.argv[4::2],sys.argv[5::2]));image=int(opts["--image"]);mode=opts["--image-mode"]
print(app,image,opts,flush=True)
if app=="conv" and image==2: sys.exit(7)
out.mkdir()
def table(name,fields,rows):
 with (out/name).open("w",newline="") as f:
  w=csv.DictWriter(f,fields);w.writeheader();w.writerows(rows)
fields=["candidate_id","active_nodes","status","visual_status","images_exported","global_mse","roi_mse","search_status"]
summary=[dict(candidate_id="baseline",active_nodes=0,status="reference",visual_status="reference",images_exported=0,global_mse=24,roi_mse=24,search_status="budget_exhausted")]
configs=[];images=[]
if app=="sobel" and image==3:
 summary.append(dict(candidate_id="n2_none",active_nodes=2,status="no_numeric_candidate",visual_status="not_available",images_exported=0,search_status="budget_exhausted"))
else:
 summary.append(dict(candidate_id="n2_rank1",active_nodes=2,status="numeric_eligible",visual_status="pending_user_review",images_exported=int(mode!="none"),global_mse=64,roi_mse=200,search_status="budget_exhausted"))
 configs=[dict(candidate_id="n2_rank1",node_id=n) for n in (0,1)]
 if mode!="none":
  for kind in ("output","roi_comparison","background_comparison"):
   name="n2_rank1_"+kind+".png";(out/name).write_bytes(b"external image")
   images.append(dict(candidate_id="n2_rank1",image_kind=kind,path=name))
table("summary.csv",fields,summary)
table("configurations.csv",["candidate_id","node_id"],configs)
table("search_stats.csv",["contexts_built","search_status"],[dict(contexts_built=12,search_status="budget_exhausted")])
table("images.csv",["candidate_id","image_kind","path"],images)
(out/"RUN.txt").write_text("completed=0\\napplication="+app+"\\nimage="+str(image)+"\\n"+("" if mode=="none" and image==4 else "completed=1\\n"))
''',encoding="utf-8");exe.chmod(0o755)
        return data,exe
    def read(self,path):
        with path.open(newline="") as f:return list(csv.DictReader(f))
    def test_default_thirty_tasks_keep_failure_and_budget_separate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,exe=self.fixture(root);out=root/"batch"
            self.assertEqual(batch.main([str(data),str(out),"--skip-build","--exe",str(exe)]),1)
            statuses=self.read(out/"run_status.csv");best=self.read(out/"best_per_image.csv")
            self.assertEqual(len(statuses),30);self.assertEqual(len(best),30)
            self.assertEqual(sum(r["status"]=="run_failed" for r in best),1)
            self.assertEqual(sum(r["status"]=="no_numeric_candidate" for r in best),1)
            self.assertEqual(sum(r["status"]=="numeric_eligible" for r in best),28)
            self.assertEqual(statuses[-1]["status"],"completed")
            images=self.read(out/"images_all.csv");self.assertEqual(len(images),84)
            self.assertTrue(all((out/r["path"]).is_file() for r in images))
            self.assertTrue(all(r["path"].startswith(r["application"]+"/") for r in images))
            self.assertFalse((out/"ablations_all.csv").exists())
            run=json.loads((out/"RUN.json").read_text());self.assertTrue(run["completed"]);self.assertFalse(run["all_succeeded"])
            original=(out/"best_per_image.csv").read_bytes()
            self.assertEqual(batch.main([str(data),str(out),"--skip-build","--exe",str(exe)]),1)
            self.assertEqual(original,(out/"best_per_image.csv").read_bytes())
    def test_partial_report_and_duplicate_applications(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,exe=self.fixture(root);out=root/"partial"
            rc=batch.main([str(data),str(out),"--skip-build","--exe",str(exe),"--applications","dct","--first","4","--last","4","--image-mode","none"])
            self.assertEqual(rc,1);self.assertEqual(self.read(out/"best_per_image.csv")[0]["status"],"run_failed")
            self.assertFalse(self.read(out/"configurations_all.csv"))
            bad=root/"bad"
            self.assertEqual(batch.main([str(data),str(bad),"--applications","conv,conv","--skip-build","--exe",str(exe)]),1)
            self.assertFalse(bad.exists())
if __name__=="__main__":unittest.main()
