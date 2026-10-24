"""Exercise exported data, independent arithmetic identities and report failure handling."""
import csv
import concurrent.futures
import math
from pathlib import Path
import subprocess
import sys
import tempfile


EXE = str(Path(sys.argv[1]).resolve())


def rows(path):
    with path.open(newline="") as f:
        result = list(csv.DictReader(f))
    assert all(None not in row and all(v is not None for v in row.values()) for row in result), path
    return result


def run(*args, ok=True):
    p = subprocess.run([EXE, *map(str, args)], capture_output=True, text=True)
    assert (p.returncode == 0) == ok, (p.returncode, p.stdout, p.stderr)
    return p


def fixture(root, empty=False):
    (root / "stage1/input").mkdir(parents=True)
    (root / "mask").mkdir()
    pixels = bytes((17*r + 31*c + 13*r*c) % 256 for r in range(6) for c in range(7))
    mask = bytes(255 if not empty and 1 <= r < 4 and 1 <= c < 4 else 0
                 for r in range(6) for c in range(7))
    # OpenCV identifies these lossless fixtures by their PGM content.
    (root / "stage1/input/image_01.jpg").write_bytes(b"P5\n7 6\n255\n" + pixels)
    (root / "mask/roi_mask.jpg").write_bytes(b"P5\n7 6\n255\n" + mask)


def close(a, b):
    assert math.isclose(float(a), b, rel_tol=1e-10, abs_tol=1e-8), (a, b)


def check_reports(root):
    data, out = root / "data", root / "reports"
    fixture(data)
    run(data, out, "--responses")
    samples = rows(out / "baseline_samples.csv")
    probes = rows(out / "bit_responses.csv")
    summary = rows(out / "bit_summary.csv")
    groups = rows(out / "monitor_groups.csv")
    assert len(samples) == 80 and len(probes) == 960 and len(summary) == 96
    lookup = {(int(s["row"]), int(s["col"]), int(s["node_id"])): s for s in samples}
    invalid = 0
    for f in probes:
        s = lookup[(int(f["row"]), int(f["col"]), int(f["node_id"]))]
        r, c = int(s["row"]), int(s["col"])
        value = lambda y, x: (17*y + 31*x + 13*y*x) % 256
        reference = max(0, min(255, 5*value(r,c)-value(r-1,c)-value(r+1,c)-value(r,c-1)-value(r,c+1)))
        assert int(s["reference_final"]) == reference
        assert int(s["exact_same_input_output"]) == int(s["input1"]) + int(s["input2"])
        assert int(s["local_error"]) == int(s["baseline_output"]) - int(s["exact_same_input_output"])
        assert int(s["accumulated_error"]) == int(s["baseline_output"]) - int(s["exact_path_output"])
        assert f["region"] == s["region"]
        original, bit = int(s["baseline_output"]), int(f["bit"])
        raw = (original & 8191) ^ (1 << bit)
        flipped = raw - 8192 if raw & 4096 else raw
        assert int(f["flipped_output"]) == flipped
        assert int(f["node_delta"]) == flipped - original
        for name in ("local", "accumulated"):
            e = int(s[name+"_error"])
            after = int(f[name+"_error_after"])
            assert int(f[name+"_delta_sse"]) == after*after-e*e
        if f["valid"] == "0":
            invalid += 1
            assert f["final_pixel"] == f["final_error"] == f["final_delta_sse"] == ""
        else:
            error = int(f["final_pixel"])-reference
            assert int(f["final_error"]) == error
            assert int(f["final_delta_sse"]) == error*error-(int(s["baseline_final"])-reference)**2
    assert invalid > 0

    indexed = {}
    for f in probes:
        key = (f["node_id"], f["bit"], f["region"])
        indexed.setdefault(key, []).append(f)
    def check_group(row, selected):
        assert int(row["samples"]) == len(selected)
        valid = [f for f in selected if f["valid"] == "1"]
        assert int(row["valid_final_samples"]) == len(valid)
        assert int(row["invalid_downstream"]) == len(selected)-len(valid)
        deltas = [int(f["final_delta_sse"]) for f in valid]
        assert int(row["final_delta_sse"]) == sum(deltas)
        assert int(row["final_increase_count"]) == sum(d > 0 for d in deltas)
        assert int(row["final_decrease_count"]) == sum(d < 0 for d in deltas)
        assert int(row["final_unchanged_count"]) == sum(d == 0 for d in deltas)
        assert int(row["final_increase_sse"]) == sum(max(d,0) for d in deltas)
        assert int(row["final_reduction_sse"]) == sum(max(-d,0) for d in deltas)
        assert int(row["local_delta_sse"]) == sum(int(f["local_delta_sse"]) for f in selected)
        local_deltas = [int(f["local_delta_sse"]) for f in selected]
        assert int(row["local_increase_sse"]) == sum(max(d,0) for d in local_deltas)
        assert int(row["local_reduction_sse"]) == sum(max(-d,0) for d in local_deltas)
        if selected:
            close(row["final_valid_rate"], len(valid)/len(selected))
        else:
            assert row["final_valid_rate"] == ""
        if valid:
            close(row["mean_final_delta_valid"], sum(deltas)/len(valid))
            close(row["final_decrease_rate_valid"], sum(d < 0 for d in deltas)/len(valid))
        else:
            assert row["mean_final_delta_valid"] == row["final_decrease_rate_valid"] == ""
        if selected:
            close(row["mean_local_delta"], sum(int(f["local_delta_sse"]) for f in selected)/len(selected))
        else:
            assert row["mean_local_delta"] == ""
    for row in summary:
        check_group(row, indexed.get((row["node_id"],row["bit"],row["region"]),[]))
    for row in groups:
        selected = indexed[(row["node_id"],row["bit"],row["region"])]
        selected = [f for f in selected if lookup[(int(f["row"]),int(f["col"]),int(f["node_id"]))][row["monitor"]] == row["monitor_value"]]
        check_group(row, selected)
    assert sum(int(g["samples"]) for g in groups) == 3*len(probes)
    image = rows(out / "image_summary.csv")[0]
    assert int(image["roi_pixels"]) == 9 and int(image["non_roi_pixels"]) == 33
    assert "completed=1" in (out / "RUN.txt").read_text()
    previous = (out / "baseline_samples.csv").read_bytes()
    run(data,out,ok=False)
    assert previous == (out / "baseline_samples.csv").read_bytes()


def check_empty_region_and_failures(root):
    data, out = root / "empty_data", root / "empty_reports"
    fixture(data, empty=True)
    run(data,out)
    assert not (out / "bit_responses.csv").exists()
    for row in rows(out / "bit_summary.csv"):
        if row["region"] == "roi":
            assert row["samples"] == "0" and row["mean_local_delta"] == row["mean_final_delta_valid"] == ""
    assert rows(out / "image_summary.csv")[0]["baseline_roi_mse"] == ""
    run(data,str(root / "trailing")+"/")
    assert len(rows(root / "trailing/baseline_samples.csv")) == 80
    run(data,str(root / "trailing_dot")+"/./")
    assert len(rows(root / "trailing_dot/baseline_samples.csv")) == 80
    for args in [("0",), ("1", "31"), ("1x",), ("--unknown",), ("2", "1")]:
        run(data,root / "bad",*args,ok=False)
    run(data,root / "missing","2",ok=False)
    assert "completed=1" not in (root / "missing/RUN.txt").read_text()
    # All contenders share a destination; precisely one process may own/write it.
    import threading
    gate = threading.Barrier(8)
    def contender(_):
        gate.wait()
        return subprocess.run([EXE,str(data),str(root / "concurrent")],capture_output=True).returncode
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        codes = list(pool.map(contender, range(8)))
    assert codes.count(0) == 1, codes
    assert len(rows(root / "concurrent/baseline_samples.csv")) == 80
    if Path("/dev/full").exists():
        fail = root / "write_failure"
        fail.mkdir()
        # A process-specific file-size limit exercises a checked report write,
        # including metadata state, without overwriting any existing directory.
        import resource
        def limit_size():
            import signal
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (4096,4096))
        p = subprocess.run([EXE,str(data),str(fail / "new")],capture_output=True,preexec_fn=limit_size)
        assert p.returncode != 0
        assert "completed=1" not in (fail / "new/RUN.txt").read_text()


with tempfile.TemporaryDirectory(prefix="bitflip-diagnostics-") as tmp:
    root = Path(tmp)
    check_reports(root)
    check_empty_region_and_failures(root)
print("Diagnostic CSVs preserve trace, grouping, validity, denominators and failure state")
