#!/usr/bin/env python3
"""Select closed trigger intervals from completed, single-probe Sharpen profiles.

No image simulation is repeated. Signed final SSE changes already include the
original downstream approximate circuit and clipping. This is per-image offline
exploration with exact references/ROI, not joint-node or frozen-hardware evidence.
"""
import argparse
import csv
from dataclasses import dataclass
import json
import math
from pathlib import Path
import sys


@dataclass(frozen=True)
class ImageStats:
    roi_pixels: int
    non_roi_pixels: int
    roi_sse: int
    non_roi_sse: int


@dataclass
class Bin:
    roi_delta: int = 0
    non_delta: int = 0
    invalid: int = 0
    roi_hits: int = 0
    non_hits: int = 0


METRICS = ["global_mse", "roi_mse", "non_roi_mse", "global_psnr", "roi_psnr",
           "non_roi_psnr", "delta_roi_mse", "delta_non_roi_mse", "advantage",
           "roi_trigger_rate", "non_roi_trigger_rate"]


def psnr(mse):
    return math.inf if mse == 0 else 10*math.log10(65025/mse)


def outcome(image, lower, upper, sr, sn, invalid, hr, hn, threshold, active=True):
    row = dict(lower=lower, upper=upper, has_interval=int(active),
               invalid_triggered=invalid, status="ok", quality_ok=0)
    if invalid:
        row.update(status="range_invalid", **{k: "" for k in METRICS})
        return row
    er, en = image.roi_sse+sr, image.non_roi_sse+sn
    if er < 0 or en < 0:
        raise ValueError("Profile implies negative regional SSE; input is inconsistent")
    nr, nn = image.roi_pixels, image.non_roi_pixels
    gm, rm, nm = (er+en)/(nr+nn), er/nr, en/nn
    row.update(global_mse=gm, roi_mse=rm, non_roi_mse=nm,
               global_psnr=psnr(gm), roi_psnr=psnr(rm), non_roi_psnr=psnr(nm),
               delta_roi_mse=sr/nr, delta_non_roi_mse=sn/nn,
               advantage=sr/nr-sn/nn, roi_trigger_rate=hr/nr, non_roi_trigger_rate=hn/nn)
    row["quality_ok"] = int(gm <= 65025*10**(-threshold/10) + 1e-12)
    if not row["quality_ok"]:
        row["status"] = "quality_failed"
    return row


def no_interval():
    return dict(lower="", upper="", has_interval=0, status="no_feasible_interval",
                quality_ok=0, invalid_triggered="", **{k: "" for k in METRICS})


def search_intervals(image, bins, boundary_budget=64, threshold=30):
    if boundary_budget < 0 or boundary_budget == 1:
        raise ValueError("Boundary budget must be 0 (exhaustive), or at least 2")
    if not math.isfinite(threshold) or not 0 <= threshold <= 1000:
        raise ValueError("PSNR threshold must be finite and within 0..1000 dB")
    if min(image.roi_pixels,image.non_roi_pixels) <= 0 or min(image.roi_sse,image.non_roi_sse) < 0:
        raise ValueError("Both image regions need positive pixel counts and nonnegative SSE")
    values = sorted(bins)
    if not values:
        raise ValueError("No monitor values in structure")
    prefix = [[0] for _ in range(5)]
    for value in values:
        b = bins[value]
        for p, x in zip(prefix, (b.roi_delta,b.non_delta,b.invalid,b.roi_hits,b.non_hits)):
            p.append(p[-1]+x)
    def change(lo,hi):
        return tuple(p[hi+1]-p[lo] for p in prefix)
    off = outcome(image,"","",0,0,0,0,0,threshold,False)
    full = outcome(image,values[0],values[-1],*change(0,len(values)-1),threshold)
    if boundary_budget == 0:
        ends = range(len(values))
    else:
        count = min(boundary_budget,len(values))
        ends = [0] if count == 1 else [i*(len(values)-1)//(count-1) for i in range(count)]
    # Reduced grids still include every singleton; narrow useful bands are common.
    def pairs():
        for index in range(len(values)):
            yield index,index
        for i,lo in enumerate(ends):
            for hi in ends[i+1:]:
                yield lo,hi
    best_a = best_b = None
    key_a = key_b = None
    evaluated = rejected = quality_rejected = 0
    limit = 65025*10**(-threshold/10)*(image.roi_pixels+image.non_roi_pixels)
    for lo,hi in pairs():
        evaluated += 1
        sr,sn,invalid,hr,hn = change(lo,hi)
        if invalid:
            rejected += 1
            continue
        total = image.roi_sse+image.non_roi_sse+sr+sn
        if total > limit + 1e-10:
            quality_rejected += 1
            continue
        # Fixed denominators permit exact integer ranking, including bias sign.
        ka = (sr,-total,-(hi-lo),-values[lo])
        kb = (-sn,sr,-total,-(hi-lo),-values[lo])
        if sr > 0 and sr*image.non_roi_pixels > sn*image.roi_pixels:
            if key_a is None or ka > key_a:
                key_a = ka
                best_a = (lo,hi,sr,sn,invalid,hr,hn)
        if sn < 0 and (key_b is None or kb > key_b):
            key_b = kb
            best_b = (lo,hi,sr,sn,invalid,hr,hn)
    def selected(best):
        if best is None:
            return no_interval()
        lo,hi,*delta = best
        return outcome(image,values[lo],values[hi],*delta,threshold)
    return dict(off=off,full=full,roi_amplify=selected(best_a),
                background_compensate=selected(best_b),evaluated=evaluated,
                range_rejected=rejected,quality_rejected=quality_rejected)


def read_csv(path):
    with path.open(newline="",encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream):
            if None in row or any(v is None for v in row.values()):
                raise ValueError(f"Malformed CSV row in {path}")
            yield row


def load_profiles(source):
    metadata = {}
    for line in (source/"RUN.txt").read_text(encoding="utf-8-sig").splitlines():
        if "=" in line:
            key,value = line.split("=",1); metadata[key] = value
    if any(metadata.get(k) != v for k,v in
           dict(completed="1",schema_version="1",baseline="all_5RP",application="Sharpening").items()):
        raise ValueError("Require completed schema-1 all-5RP Sharpening diagnostic reports")
    images = {}
    for row in read_csv(source/"image_summary.csv"):
        index = int(row["image"])
        if index in images:
            raise ValueError("Duplicate image summary")
        images[index] = ImageStats(*(int(row[k]) for k in
            ("roi_pixels","non_roi_pixels","roi_sse","non_roi_sse")))
    expected = {}
    for row in read_csv(source/"bit_summary.csv"):
        key = (int(row["image"]),int(row["node_id"]),int(row["bit"]),row["region"])
        if key in expected:
            raise ValueError("Duplicate bit summary")
        expected[key] = tuple(int(row[k]) for k in
            ("samples","valid_final_samples","invalid_downstream","final_delta_sse","baseline_final_sse_valid"))
    valid_summaries = {(i,n,b,r) for i in images for n in range(4) for b in range(12)
                       for r in ("roi","non_roi")}
    if not images or set(expected) != valid_summaries:
        raise ValueError("Missing or unexpected image/node/bit/region summaries")
    structures, totals, seen = {}, {}, set()
    for row in read_csv(source/"monitor_groups.csv"):
        image,node,bit = (int(row[k]) for k in ("image","node_id","bit"))
        region,monitor,value = row["region"],row["monitor"],int(row["monitor_value"])
        summary_key = (image,node,bit,region)
        if summary_key not in expected or monitor not in ("input1","input2","baseline_output") or not -4096 <= value <= 4095:
            raise ValueError("Unexpected monitor group domain")
        key = (image,node,bit,monitor)
        identity = key+(region,value)
        if identity in seen:
            raise ValueError("Duplicate monitor value/region group")
        seen.add(identity)
        counts = [int(row[k]) for k in
            ("samples","valid_final_samples","invalid_downstream","final_delta_sse","baseline_final_sse_valid")]
        samples,valid,invalid,delta,baseline = counts
        if min(samples,valid,invalid,baseline) < 0 or samples != valid+invalid or baseline+delta < 0 or (not valid and (baseline or delta)):
            raise ValueError("Inconsistent group counts or final SSE")
        total = totals.setdefault(key+(region,),[0]*5)
        for j,x in enumerate(counts):
            total[j] += x
        bin_ = structures.setdefault(key,{}).setdefault(value,Bin())
        if region == "roi":
            bin_.roi_delta += delta; bin_.roi_hits += samples
        else:
            bin_.non_delta += delta; bin_.non_hits += samples
        bin_.invalid += invalid
    for (image,node,bit,region), expected_counts in expected.items():
        stats = images[image]
        count = stats.roi_pixels if region == "roi" else stats.non_roi_pixels
        sse = stats.roi_sse if region == "roi" else stats.non_roi_sse
        if expected_counts[0] > count or expected_counts[4] > sse:
            raise ValueError("Profile totals exceed whole-image region")
        for monitor in ("input1","input2","baseline_output"):
            if tuple(totals.get((image,node,bit,monitor,region),[0]*5)) != expected_counts:
                raise ValueError("Monitor groups do not reconcile with bit summary")
    expected_structures = {(i,n,b,m) for i in images for n in range(4) for b in range(12)
                           for m in ("input1","input2","baseline_output")}
    if set(structures) != expected_structures:
        raise ValueError("Missing monitor structures")
    return images,structures


FIELDS = ["image","node_id","node_name","bit","monitor","mode","lower","upper",
          "has_interval","status","quality_ok","invalid_triggered",*METRICS,
          "evaluated_intervals","range_rejected_intervals","quality_rejected_intervals"]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profiles",type=Path)
    parser.add_argument("output",type=Path)
    parser.add_argument("--boundary-budget",type=int,default=64)
    parser.add_argument("--global-psnr",type=float,default=30)
    args = parser.parse_args(argv)
    try:
        if args.boundary_budget < 0 or args.boundary_budget == 1 or not math.isfinite(args.global_psnr) or not 0 <= args.global_psnr <= 1000:
            raise ValueError("Use budget=0 or >=2 and a finite PSNR within 0..1000")
        images,structures = load_profiles(args.profiles)
        args.output.mkdir(parents=True,exist_ok=False)
        metadata = dict(completed=False,source=str(args.profiles.resolve()),
                        boundary_budget=args.boundary_budget,global_psnr=args.global_psnr,
                        selection="per-image offline exploration; independent baseline probes",
                        local_execution="not performed by the authoring agent; run by user")
        def save_metadata():
            (args.output/"RUN.json").write_text(json.dumps(metadata,indent=2)+"\n",encoding="utf-8")
        save_metadata()
        best = {}
        with (args.output/"comparisons.csv").open("w",newline="",encoding="utf-8") as stream:
            writer = csv.DictWriter(stream,FIELDS);writer.writeheader()
            for (image,node,bit,monitor),bins in sorted(structures.items()):
                result = search_intervals(images[image],bins,args.boundary_budget,args.global_psnr)
                for mode in ("off","full","roi_amplify","background_compensate"):
                    row = dict(image=image,node_id=node,node_name=f"A{node+1}",bit=bit,monitor=monitor,mode=mode,
                               **result[mode],evaluated_intervals=result["evaluated"],
                               range_rejected_intervals=result["range_rejected"],
                               quality_rejected_intervals=result["quality_rejected"])
                    writer.writerow(row)
                    if mode not in ("roi_amplify","background_compensate") or not row["has_interval"]:
                        continue
                    key = (image,mode)
                    rank = (row["delta_roi_mse"],-row["global_mse"]) if mode == "roi_amplify" else (
                        -row["delta_non_roi_mse"],row["delta_roi_mse"],-row["global_mse"])
                    if key not in best or rank > best[key][0]:
                        best[key] = (rank,row)
        with (args.output/"selected_intervals.csv").open("w",newline="",encoding="utf-8") as stream:
            writer = csv.DictWriter(stream,FIELDS);writer.writeheader()
            for image in sorted(images):
                for mode in ("roi_amplify","background_compensate"):
                    entry = best.get((image,mode))
                    row = entry[1] if entry else dict(image=image,mode=mode,**no_interval())
                    writer.writerow(row)
                    if entry:
                        print(f"Image {image} {mode}: {row['node_name']}/{row['monitor']}/bit{row['bit']} "
                              f"[{row['lower']},{row['upper']}], Global={row['global_psnr']:.3f} dB, "
                              f"delta ROI/background MSE={row['delta_roi_mse']:.6g}/{row['delta_non_roi_mse']:.6g}")
                    else:
                        print(f"Image {image} {mode}: NO FEASIBLE INTERVAL")
        metadata["completed"] = True;save_metadata()
        return 0
    except (OSError,ValueError,KeyError,csv.Error) as exc:
        print(f"Interval selection failed: {exc}",file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
