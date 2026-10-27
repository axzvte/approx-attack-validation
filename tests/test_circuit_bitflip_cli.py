"""User-run CLI integration. Reject export-policy metric drift and false success."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

exe=str(Path(sys.argv[1]).resolve())
def rows(path):
    with path.open(newline="") as f: return list(csv.DictReader(f))
def run(app,data,out,*args,ok=True):
    p=subprocess.run([exe,app,str(data),str(out),*map(str,args)],capture_output=True,text=True)
    assert (p.returncode==0)==ok,(p.returncode,p.stdout,p.stderr)
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);data=root/"data";(data/"stage1/input").mkdir(parents=True);(data/"mask").mkdir()
    (data/"stage1/input/image_01.jpg").write_bytes(b"P5\n10 9\n255\n"+bytes((17*r+31*c+13*r*c)%256 for r in range(9) for c in range(10)))
    (data/"mask/roi_mask.jpg").write_bytes(b"P5\n10 9\n255\n"+bytes(255 if 3<=r<7 and 4<=c<8 else 0 for r in range(9) for c in range(10)))
    common=["--beam",1,"--refinement",0,"--boundary-budget",4,"--top-per-count",1,
            "--restarts",1,"--global-psnr",0,"--max-contexts",12,"--max-full-evals",80,
            "--dct-full-candidates",4,"--bits","0,2"]
    def metrics(out):
        return [(r["candidate_id"],r["status"],r["global_mse"],r["roi_mse"],r["non_roi_mse"]) for r in rows(out/"summary.csv")]
    for app in ("conv","sobel","dct"):
        out=root/app;run(app,data,out,*common)
        assert "completed=1" in (out/"RUN.txt").read_text()
        summary=rows(out/"summary.csv");assert summary[0]["status"]=="reference"
        numeric=[r for r in summary if r["status"]=="numeric_eligible"]
        assert all(r["visual_status"]=="pending_user_review" for r in numeric)
        manifest=rows(out/"images.csv")
        assert len(manifest)==(3 if numeric else 0)
        if numeric:
            assert {r["image_kind"] for r in manifest}=={"output","roi_comparison","background_comparison"}
            assert len({r["candidate_id"] for r in manifest})==1
            chosen=next(r for r in numeric if r["images_exported"]=="1")
            assert float(chosen["roi_mse"])==max(float(r["roi_mse"]) for r in numeric)
        assert all((out/r["path"]).stat().st_size>0 for r in manifest)
        assert not (out/"ablations.csv").exists() and not list(out.glob("*without*"))
        configs=rows(out/"configurations.csv")
        for r in numeric:
            cs=[c for c in configs if c["candidate_id"]==r["candidate_id"]]
            assert len(cs)==int(r["active_nodes"])>=2
            assert all(int(c["dynamic_triggers"])>0 for c in cs)
        none=root/(app+"_none");run(app,data,none,*common,"--image-mode","none")
        assert metrics(none)==metrics(out) and not list(none.glob("*.png"))
        full=root/(app+"_full");run(app,data,full,*common,"--image-mode","full")
        assert metrics(full)==metrics(out) and (full/"baseline.png").is_file()
        previous=(out/"summary.csv").read_bytes();run(app,data,out,*common,ok=False)
        assert previous==(out/"summary.csv").read_bytes()
    impossible=list(common);impossible[impossible.index("--global-psnr")+1]=100
    empty=root/"no_numeric";run("conv",data,empty,*impossible)
    assert not any(r["status"]=="numeric_eligible" for r in rows(empty/"summary.csv"))
    assert any(r["status"]=="no_numeric_candidate" for r in rows(empty/"summary.csv"))
    assert not list(empty.glob("*.png"))
    run("conv",data,root/"duplicate",*common,"--global-psnr",100,ok=False)
    run("conv",data,root/"bad","--bits","12",ok=False)
    run("conv",data,root/"bad","--max-active",1,ok=False)
    run("unknown",data,root/"bad",ok=False)
print("Cross-circuit CLI exports and no-ablation contracts passed")
