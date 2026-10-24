"""Test batch orchestration with a controlled external report producer.

The real C++ image/export behavior is covered by test_sharpen_bitflip_joint_cli.py.
Execution of both tests belongs to the user.
"""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

script=Path(__file__).resolve().parents[1]/"scripts/run_sharpen_bitflip_batch.py"
spec=importlib.util.spec_from_file_location("joint_batch",script)
batch=importlib.util.module_from_spec(spec);spec.loader.exec_module(batch)


class BatchTests(unittest.TestCase):
    def fixture(self,root):
        data=root/"data";(data/"mask").mkdir(parents=True)
        (data/"mask/roi_mask.jpg").write_bytes(b"fixture")
        exe=root/"fake_joint"
        exe.write_text('''#!/usr/bin/env python3
import csv
from pathlib import Path
import sys
out=Path(sys.argv[2]);image=int(sys.argv[3]);mode=sys.argv[-1]
print("image",image,"beam",sys.argv[4],"mode",mode,flush=True)
if image==2: sys.exit(7)
out.mkdir()
def table(name,fields,rows):
 with (out/name).open("w",newline="") as f:
  writer=csv.DictWriter(f,fields);writer.writeheader();writer.writerows(rows)
fields=["candidate_id","active_nodes","status","visual_status","images_exported","global_mse","roi_mse"]
summary=[dict(candidate_id="baseline",active_nodes=0,status="reference",visual_status="reference",images_exported=0,global_mse=24,roi_mse=24)]
configs=[];ablations=[];images=[]
if image==3:
 for count in (2,3,4): summary.append(dict(candidate_id="n%d_none"%count,active_nodes=count,status="no_numeric_candidate",visual_status="not_available",images_exported=0))
else:
 summary.append(dict(candidate_id="n3_rank1",active_nodes=3,status="numeric_eligible",visual_status="pending_user_review",images_exported=int(mode!="none"),global_mse=64,roi_mse=193))
 configs=[dict(candidate_id="n3_rank1",node_id=n) for n in (0,1,3)]
 ablations=[dict(candidate_id="n3_rank1",removed_node=n,status="evaluated") for n in (0,1,3)]
 if mode!="none":
  for kind in ("output","roi_comparison","background_comparison"):
   name="n3_rank1_"+kind+".png";(out/name).write_bytes(b"external fixture image")
   images.append(dict(candidate_id="n3_rank1",image_kind=kind,path=name))
table("summary.csv",fields,summary)
table("configurations.csv",["candidate_id","node_id"],configs)
table("ablations.csv",["candidate_id","removed_node","status"],ablations)
table("search_stats.csv",["contexts_built"],[dict(contexts_built=10)])
table("images.csv",["candidate_id","image_kind","path"],images)
(out/"RUN.txt").write_text("completed=0\\n"+("" if mode=="none" and image==4 else "completed=1\\n"))
''',encoding="utf-8")
        exe.chmod(0o755)
        return data,exe

    def read(self,path):
        with path.open(newline="") as f: return list(csv.DictReader(f))

    def test_default_ten_images_continue_after_failure_and_merge(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,exe=self.fixture(root);out=root/"batch"
            rc=batch.main([str(data),str(out),"--exe",str(exe),"--skip-build","--beam","2"])
            self.assertEqual(rc,1)  # image2 failed, but all ten images attempted
            status=self.read(out/"run_status.csv")
            self.assertEqual([int(r["image"]) for r in status],list(range(1,11)))
            self.assertEqual(status[1]["status"],"failed")
            self.assertEqual(status[-1]["status"],"completed")
            best=self.read(out/"best_per_image.csv")
            self.assertEqual(len(best),10)
            self.assertEqual(best[1]["status"],"run_failed")
            self.assertEqual(best[2]["status"],"no_numeric_candidate")
            self.assertEqual(best[0]["roi_mse"],"193")
            self.assertEqual(len(self.read(out/"configurations_all.csv")),8*3)
            self.assertEqual(len(self.read(out/"ablations_all.csv")),8*3)
            image_rows=self.read(out/"images_all.csv")
            self.assertEqual(len(image_rows),8*3)
            self.assertTrue(all((out/r["path"]).is_file() for r in image_rows))
            self.assertIn("beam 2 mode compact",(out/"logs/image_01.log").read_text())
            metadata=json.loads((out/"RUN.json").read_text())
            self.assertTrue(metadata["completed"])
            self.assertFalse(metadata["all_succeeded"])
            self.assertEqual(metadata["failed_images"],[2])
            previous=(out/"best_per_image.csv").read_bytes()
            self.assertEqual(batch.main([str(data),str(out),"--exe",str(exe),"--skip-build"]),1)
            self.assertEqual(previous,(out/"best_per_image.csv").read_bytes())

    def test_incomplete_report_is_failure_not_a_numeric_result(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,exe=self.fixture(root);out=root/"partial"
            rc=batch.main([str(data),str(out),"--exe",str(exe),"--skip-build","--first","4","--last","4","--image-mode","none"])
            self.assertEqual(rc,1)
            self.assertEqual(self.read(out/"best_per_image.csv")[0]["status"],"run_failed")
            self.assertFalse(self.read(out/"configurations_all.csv"))
            self.assertFalse(self.read(out/"images_all.csv"))

    def test_bad_image_range_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,exe=self.fixture(root);out=root/"bad"
            self.assertEqual(batch.main([str(data),str(out),"--exe",str(exe),"--skip-build","--last","31"]),1)
            self.assertFalse(out.exists())


if __name__=="__main__": unittest.main()
