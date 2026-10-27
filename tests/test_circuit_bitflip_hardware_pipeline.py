"""User-run only. Shared fixed-hardware protocol with controlled circuit reports."""
import csv
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

scripts=Path(__file__).resolve().parents[1]/"scripts";sys.path.insert(0,str(scripts))
import run_sharpen_bitflip_hardware as protocol
import run_circuit_bitflip_hardware as batch
import test_sharpen_bitflip_hardware_pipeline as sharpen_tests

def read(path):
    with path.open(newline="") as stream:return list(csv.DictReader(stream))
def table(path,rows):
    with path.open("w",newline="") as stream:
        w=csv.DictWriter(stream,list(rows[0]));w.writeheader();w.writerows(rows)

class CircuitHardwareTests(unittest.TestCase):
    def fixture(self,root):
        data,source,exe=sharpen_tests.PipelineTests().fixture(root)
        summaries=read(source/"summary_all.csv");configs=read(source/"configurations_all.csv")
        ss=[];cs=[]
        for app,nodes in (("conv",8),("sobel",11),("dct",32)):
            ss.extend(dict(application=app,**r) for r in summaries)
            for r in configs:
                row=dict(r,application=app,node_id=nodes-2+int(r["node_id"]),node_name="native_"+r["node_id"])
                cs.append(row)
        table(source/"summary_all.csv",ss);table(source/"configurations_all.csv",cs)
        text=exe.read_text().replace('out=Path(sys.argv[2]);opts=dict(zip(sys.argv[3::2],sys.argv[4::2]));',
            'application=sys.argv[1];out=Path(sys.argv[3]);opts=dict(zip(sys.argv[4::2],sys.argv[5::2]));')
        text=text.replace('table("summary.csv",list(summary[0]),summary)','for row in summary:row["application"]=application\ntable("summary.csv",list(summary[0]),summary)')
        text=text.replace('(out/"RUN.txt").write_text("completed=0\\nimage="',
            '(out/"RUN.txt").write_text("application="+application+"\\ncompleted=0\\nimage="')
        exe.write_text(text);return data,source,exe
    def argv(self,data,source,exe,out):
        return [str(data),str(out),"--training-report",str(source),"--train","1:2","--screen","3:4",
                "--skip-build","--fixed-exe",str(exe),"--max-hardware","0","--screen-workers","2"]
    def test_native_shapes_and_mixed_source_selection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root)
            for app,nodes in (("conv",8),("sobel",11),("dct",32)):
                pool,_=protocol.training_pool(source,[1,2],30,application=app,native_nodes=nodes)
                self.assertEqual(len(pool),2)
                self.assertEqual({n for n,_,_ in pool[0]["key"]},{nodes-2,nodes-1})
                out=root/app
                self.assertEqual(protocol.main(self.argv(data,source,exe,out),application=app),0)
                self.assertEqual({int(r["node_id"]) for r in read(out/"frozen_hardware.csv")},{nodes-2,nodes-1})
                self.assertEqual([int(r["image"]) for r in read(out/"best_per_image.csv")],list(range(31,41)))
                self.assertEqual(len(read(out/"images_all.csv")),30)
                self.assertEqual(json.loads((out/"frozen_protocol.json").read_text())["application"],app)
            with self.assertRaises(ValueError):protocol.training_pool(source,[1,2],30)
            full=[dict(node_id=n,monitor="input1",bit=0) for n in range(32)]
            self.assertEqual(len(protocol.hardware_key(full,native_nodes=32)),32)
    def test_batch_three_apps_same_protocol_and_namespaced_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root);out=root/"all"
            self.assertEqual(batch.main(self.argv(data,source,exe,out)),0)
            run=json.loads((out/"RUN.json").read_text());self.assertTrue(run["all_succeeded"])
            self.assertEqual(len(read(out/"best_per_image.csv")),30)
            images=read(out/"images_all.csv");self.assertEqual(len(images),90)
            self.assertEqual({r["application"] for r in images},{"conv","sobel","dct"})
            self.assertTrue(all(r["path"].startswith(r["application"]+"/") for r in images))
            for app in ("conv","sobel","dct"):
                self.assertTrue((out/app/"frozen_hardware.csv").is_file())
    def test_off_application_test_leak_is_not_silently_ignored(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root)
            rows=read(source/"summary_all.csv");rows[-1]["image"]="31";table(source/"summary_all.csv",rows)
            with self.assertRaises(ValueError):protocol.training_pool(source,[1,2],30,application="conv",native_nodes=8)
            self.assertEqual(batch.main(self.argv(data,source,exe,root/"leak")),1)
            self.assertFalse((root/"leak").exists())
    def test_screen_failures_block_each_freeze_and_remain_visible(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root);(root/"fail_screen").write_text("failure")
            out=root/"failed"
            self.assertEqual(batch.main(self.argv(data,source,exe,out)),1)
            for app in ("conv","sobel","dct"):
                self.assertFalse((out/app/"frozen_hardware.csv").exists())
                self.assertEqual(len(read(out/app/"screening_results.csv")),4)
            self.assertFalse(json.loads((out/"RUN.json").read_text())["all_succeeded"])
    def test_wrapper_interrupt_stops_before_the_next_application(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root);out=root/"interrupt"
            with patch.object(protocol,"main",return_value=130) as child:
                self.assertEqual(batch.main(self.argv(data,source,exe,out)),130)
                self.assertEqual(child.call_count,1)
            self.assertFalse(json.loads((out/"RUN.json").read_text())["completed"])
            self.assertEqual(read(out/"applications.csv")[0]["status"],"interrupted")
    def test_normal_no_candidate_does_not_fabricate_hardware_or_test_results(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root);out=root/"no_candidate"
            summaries=read(source/"summary_all.csv")
            for row in summaries:
                if row["status"]!="reference":row.update(status="no_numeric_candidate",global_mse="",roi_mse="")
            table(source/"summary_all.csv",summaries)
            fields=list(read(source/"configurations_all.csv")[0])
            with (source/"configurations_all.csv").open("w",newline="") as stream:csv.DictWriter(stream,fields).writeheader()
            self.assertEqual(batch.main(self.argv(data,source,exe,out)),0)
            self.assertFalse(read(out/"best_per_image.csv"));self.assertFalse(read(out/"images_all.csv"))
            for app in ("conv","sobel","dct"):
                self.assertFalse((out/app/"frozen_hardware.csv").exists())
                self.assertEqual(json.loads((out/app/"RUN.json").read_text())["selection_status"],"no_candidate_hardware")
    def test_sharpen_retest_rejects_a_different_application(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fixture=sharpen_tests.PipelineTests();data,source,exe=fixture.fixture(root);out=root/"sharpen"
            self.assertEqual(protocol.main(fixture.args(data,source,exe,out)),0)
            for name in ("RUN.json","frozen_protocol.json"):
                obj=json.loads((out/name).read_text());obj["application"]="conv";(out/name).write_text(json.dumps(obj))
            with self.assertRaises(ValueError):sharpen_tests.retest.frozen_source(out)
    def test_fresh_discovery_rejects_wrong_run_image_before_relabelling(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,exe=self.fixture(root);out=root/"wrong_discovery"
            argv=self.argv(data,source,exe,out);pos=argv.index("--training-report");del argv[pos:pos+2]
            argv.extend(["--discovery-exe",str(exe)])
            def discovery(command,log,cwd,**kwargs):
                report=Path(command[3]);report.mkdir();image=int(command[command.index("--image")+1])
                (report/"RUN.txt").write_text(f"completed=1\napplication=conv\nimage={image+1}\n")
                table(report/"summary.csv",[dict(application="conv",candidate_id="baseline",status="reference",global_mse=20,roi_mse=20)])
                with (report/"configurations.csv").open("w",newline="") as stream:
                    csv.DictWriter(stream,["candidate_id","node_id","monitor","bit","lower","upper"]).writeheader()
                return 0
            with patch.object(protocol,"process",side_effect=discovery):
                self.assertEqual(protocol.main(argv,application="conv"),1)
            self.assertFalse((out/"frozen_hardware.csv").exists())
            self.assertEqual(len(json.loads((out/"RUN.json").read_text())["failed_tasks"]),2)

if __name__=="__main__":unittest.main()
