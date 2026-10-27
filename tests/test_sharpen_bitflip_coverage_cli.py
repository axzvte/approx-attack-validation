"""User-run checks only. Authoring agents must not execute this file."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())

def rows(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))

def run(data, out, *args, ok=True):
    result = subprocess.run([exe, str(data), str(out), *map(str, args)], capture_output=True, text=True)
    assert (result.returncode == 0) == ok, (result.stdout, result.stderr)

with tempfile.TemporaryDirectory(prefix="joint-coverage-cli-") as temporary:
    root = Path(temporary)
    data = root / "data"
    (data / "stage1/input").mkdir(parents=True)
    (data / "mask").mkdir()
    image = bytes((17*r+31*c+13*r*c) % 256 for r in range(6) for c in range(7))
    mask = bytes(255 if 1 <= r < 4 and 1 <= c < 4 else 0 for r in range(6) for c in range(7))
    (data / "stage1/input/image_01.jpg").write_bytes(b"P5\n7 6\n255\n" + image)
    (data / "mask/roi_mask.jpg").write_bytes(b"P5\n7 6\n255\n" + mask)
    bounded = root / "bounded"
    run(data, bounded, "--context-budget", 4, 1, 1, 0, 4, 1, 2, 0,
        "--no-ablations", "--image-mode", "none")
    stats = rows(bounded / "search_stats.csv")[0]
    assert int(stats["contexts_built"]) == 4
    assert stats["context_budget"] == "4" and stats["context_budget_exhausted"] == "1"
    assert all(stats[f"contexts_node_{node}"] == "1" for node in range(4))
    assert not (bounded / "ablations.csv").exists()
    notes = (bounded / "RUN.txt").read_text()
    assert "ablations=disabled" in notes and "context_budget=4" in notes and "completed=1" in notes
    assert not any(r["status"] == "numeric_eligible" for r in rows(bounded / "summary.csv"))
    (data / "stage4/input").mkdir(parents=True)
    (data / "stage4/input/image_40.jpg").write_bytes((data / "stage1/input/image_01.jpg").read_bytes())
    stage4=root/"stage4"
    run(data,stage4,40,1,0,4,1,2,0,"--context-budget",4,"--no-ablations","--image-mode","none")
    assert (stage4/"summary.csv").read_bytes()==(bounded/"summary.csv").read_bytes()
    assert "image=40" in (stage4/"RUN.txt").read_text()
    (data/"stage4/input/image_40.jpg").unlink()
    (data/"stage4/image_40.jpg").write_bytes((data/"stage1/input/image_01.jpg").read_bytes())
    flat=root/"flat_stage4"
    run(data,flat,40,1,0,4,1,2,0,"--context-budget",4,"--no-ablations","--image-mode","none")
    assert (flat/"summary.csv").read_bytes()==(bounded/"summary.csv").read_bytes()
    run(data,root/"bad41",41,ok=False)
    # With identical search settings, disabling ablations changes no source reports.
    default = root / "default"
    disabled = root / "disabled"
    positional = (1, 1, 0, 4, 1, 1, 0)
    run(data, default, *positional, "--image-mode", "none")
    run(data, disabled, *positional, "--image-mode", "none", "--no-ablations", "--context-budget", 0)
    for name in ("summary.csv", "configurations.csv", "search_stats.csv"):
        assert (default / name).read_bytes() == (disabled / name).read_bytes(), name
    assert (default / "ablations.csv").exists() and not (disabled / "ablations.csv").exists()
    assert any(r["status"] == "numeric_eligible" for r in rows(disabled / "summary.csv"))
    for index, arguments in enumerate([
        ("--context-budget", "1"), ("--context-budget", "2"), ("--context-budget", "3"),
        ("--context-budget", "-1"), ("--context-budget", "4x"), ("--context-budget",),
        ("--context-budget", "4", "--context-budget", "4"),
        ("--no-ablations", "--no-ablations"),
    ]):
        bad = root / f"bad-{index}"
        run(data, bad, *arguments, ok=False)
        assert not bad.exists(), "Invalid arguments must not claim an output directory"
print("Discovery coverage budgets and optional ablations checked")
