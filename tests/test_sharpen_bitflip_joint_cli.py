"""User-run integration checks; authoring agents must not execute this file."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

exe=str(Path(sys.argv[1]).resolve())

def rows(path):
    with path.open(newline="") as f:
        result=list(csv.DictReader(f))
    assert all(None not in r and all(v is not None for v in r.values()) for r in result),path
    return result

def run(data,out,*args,ok=True):
    p=subprocess.run([exe,str(data),str(out),*map(str,args)],capture_output=True,text=True)
    assert (p.returncode==0)==ok,(p.returncode,p.stdout,p.stderr)

with tempfile.TemporaryDirectory(prefix="joint-bitflip-cli-") as tmp:
    root=Path(tmp);data=root/"data";out=root/"trial"
    (data/"stage1/input").mkdir(parents=True);(data/"mask").mkdir()
    image=bytes((17*r+31*c+13*r*c)%256 for r in range(6) for c in range(7))
    mask=bytes(255 if 1<=r<4 and 1<=c<4 else 0 for r in range(6) for c in range(7))
    (data/"stage1/input/image_01.jpg").write_bytes(b"P5\n7 6\n255\n"+image)
    (data/"mask/roi_mask.jpg").write_bytes(b"P5\n7 6\n255\n"+mask)
    # Small search, permissive final threshold ensures report paths are exercised.
    run(data,out,1,1,0,4,1,1,0)
    summary=rows(out/"summary.csv")
    numeric=[r for r in summary if r["status"]=="numeric_eligible"]
    assert numeric,"Fixture must produce a multi-node candidate"
    configs=rows(out/"configurations.csv")
    for row in numeric:
        assert row["visual_status"]=="pending_user_review"
        assert 2<=int(row["active_nodes"])<=4
        selected=[c for c in configs if c["candidate_id"]==row["candidate_id"]]
        assert len(selected)==int(row["active_nodes"])
        assert len({c["node_id"] for c in selected})==len(selected)
        assert all(int(c["roi_triggers"])+int(c["non_roi_triggers"])>0 for c in selected)
        for suffix in ("output.png","comparison.png","roi_comparison.png","background_comparison.png","diff_baseline_x4.png"):
            assert (out/(row["candidate_id"]+"_"+suffix)).stat().st_size>0
    ablations=rows(out/"ablations.csv")
    assert len(ablations)==sum(int(r["active_nodes"]) for r in numeric)
    assert rows(out/"tiles.csv") and "completed=1" in (out/"RUN.txt").read_text()
    original=(out/"summary.csv").read_bytes()
    run(data,out,ok=False)
    assert original==(out/"summary.csv").read_bytes()
    run(data,root/"empty",1,1,0,4,1,1,100)
    missing=rows(root/"empty/summary.csv")
    assert {int(r["active_nodes"]) for r in missing if r["status"]=="no_numeric_candidate"}=={2,3,4}
    assert not any(r["status"]=="numeric_eligible" for r in missing)
    for args in [(0,),(1,0),(1,1,0,1),("1x",)]:
        run(data,root/"bad",*args,ok=False)
print("Joint reports preserve numeric/visual distinction, active-node counts and failures")
