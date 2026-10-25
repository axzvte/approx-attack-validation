"""User-run integration: freeze node/monitor/bit and keep zero-hit hardware."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

exe=str(Path(sys.argv[1]).resolve())
def table(path,fields,rows):
    with path.open("w",newline="") as f:
        w=csv.DictWriter(f,fields);w.writeheader();w.writerows(rows)
def rows(path):
    with path.open(newline="") as f:return list(csv.DictReader(f))
def run(data,out,hardware,*args,ok=True):
    p=subprocess.run([exe,str(data),str(out),"--hardware",str(hardware),*map(str,args)],capture_output=True,text=True)
    assert (p.returncode==0)==ok,(p.returncode,p.stdout,p.stderr)
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);data=root/"data";(data/"stage1/input").mkdir(parents=True);(data/"mask").mkdir()
    (data/"stage1/input/image_01.jpg").write_bytes(b"P5\n5 5\n255\n"+bytes([40]*25))
    (data/"mask/roi_mask.jpg").write_bytes(b"P5\n5 5\n255\n"+bytes(255 if i==12 else 0 for i in range(25)))
    hardware=root/"hardware.csv"
    h=[dict(node_id=0,monitor="input1",bit=0),dict(node_id=1,monitor="input2",bit=1),dict(node_id=3,monitor="baseline_output",bit=2)]
    table(hardware,["node_id","monitor","bit"],h)
    common=["--beam",1,"--refinement",0,"--restarts",1,"--boundary-budget",3,"--context-budget",1,"--global-psnr",1000]
    out=root/"trial";run(data,out,hardware,*common)
    summary=rows(out/"summary.csv");selected=[r for r in summary if r["candidate_id"]!="baseline"]
    assert len(selected)==1 and selected[0]["status"]=="quality_not_met"
    assert int(selected[0]["installed_nodes"])==3
    configs=rows(out/"configurations.csv")
    assert {(int(c["node_id"]),c["monitor"],int(c["bit"])) for c in configs}=={(x["node_id"],x["monitor"],x["bit"]) for x in h}
    assert any(int(c["dynamic_triggers"])==0 for c in configs),"A non-triggering installed node must stay in the export"
    assert not (out/"ablations.csv").exists() and not list(out.glob("*without*"))
    assert "completed=1" in (out/"RUN.txt").read_text()
    assert rows(out/"search_stats.csv")[0]["context_budget_exhausted"]=="1"
    def metrics(folder):
        return [(r["status"],r["global_mse"],r["roi_mse"],r["non_roi_mse"]) for r in rows(folder/"summary.csv")]
    none=root/"none";run(data,none,hardware,*common,"--image-mode","none")
    assert metrics(out)==metrics(none) and not list(none.glob("*.png"))
    full=root/"full";run(data,full,hardware,*common,"--image-mode","full")
    assert metrics(out)==metrics(full)
    original=(out/"summary.csv").read_bytes();run(data,out,hardware,*common,ok=False)
    assert original==(out/"summary.csv").read_bytes()
    malformed=root/"bad.csv";table(malformed,["node_id","monitor","bit"],[h[0],h[0]])
    run(data,root/"bad",malformed,ok=False)
    wrong=root/"wrong_seeds.csv"
    table(wrong,["seed_id","node_id","monitor","bit","lower","upper"],
          [dict(seed_id="s0",node_id=x["node_id"],monitor=x["monitor"],bit=7,lower=0,upper=0) for x in h])
    run(data,root/"badseed",hardware,"--seeds",wrong,ok=False)
    outside=root/"outside.csv"
    table(outside,["seed_id","node_id","monitor","bit","lower","upper"],
          [dict(seed_id="s0",node_id=x["node_id"],monitor=x["monitor"],bit=x["bit"],lower=9000,upper=9000) for x in h])
    run(data,root/"outside",hardware,"--seeds",outside,ok=False)
print("Fixed-hardware reporting and no-enable/no-ablation contracts passed")
