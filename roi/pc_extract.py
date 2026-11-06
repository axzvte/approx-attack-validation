#!/usr/bin/env python3
"""Read-only phase-congruency pilot on the existing PASCAL-S/U2-Net dataset.

No model inference, mask rewriting, region optimization, or hardware changes.
Output directories must be new or empty. Fractions in CSV are in [0, 1].
"""
from __future__ import annotations

import argparse
import csv
import json
import random
from importlib.metadata import PackageNotFoundError, version
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageOps

from evaluate_pascal_s import check_shape, hot_region, read_gray, read_rgb

DATA_DEFAULT = Path.home() / "datasets" / "PASCAL-S"
PC_PARAMS = {
    "nscale": 4,
    "minwavelength": 3,
    "mult": 2.1,
    "sigmaonf": 0.55,
    "k": 2.0,
    "noisemethod": -1,
}


def split_ids(seed: int) -> dict[str, list[int]]:
    ids = list(range(1, 851))
    random.Random(seed).shuffle(ids)
    return {
        "train": sorted(ids[:600]),
        "val": sorted(ids[600:700]),
        "test": sorted(ids[700:]),
        "all": sorted(ids),
    }


def select_ids(split: str, count: int, seed: int) -> list[int]:
    pool = split_ids(seed)[split]
    if count < 1 or count > len(pool):
        raise ValueError(f"--count for {split} must be in [1, {len(pool)}]")
    # Deterministic sample from a fixed data split; no GT or gaze used.
    if count == len(pool):
        return pool
    return sorted(random.Random(seed + 1).sample(pool, count))


def mass_fraction(response: np.ndarray, mask: np.ndarray) -> float | None:
    if response.shape != mask.shape:
        raise ValueError("Response and mask shapes differ")
    total = float(np.sum(response, dtype=np.float64))
    if not np.isfinite(total) or total <= 1e-12:
        return None
    return float(np.sum(response[mask], dtype=np.float64) / total)


def enrichment(fraction: float | None, area: float) -> float | None:
    return None if fraction is None or area <= 0 else fraction / area


def compute_pc(gray: np.ndarray) -> np.ndarray:
    """Use the unnormalized original 8-bit grayscale intensities."""
    try:
        from phasecongruency import phasecongmono
    except ImportError as exc:
        raise RuntimeError("Install: python -m pip install phasecongruency==1.0.5") from exc
    raw, _orientation, _phase_type, _threshold = phasecongmono(
        np.asarray(gray, dtype=np.float64), backend="numpy", **PC_PARAMS
    )
    raw = np.asarray(raw, dtype=np.float64)
    if raw.shape != gray.shape:
        raise ValueError(f"PC returned incorrect dimensions: {raw.shape} vs {gray.shape}")
    if not np.all(np.isfinite(raw)):
        raise ValueError("PC returned NaN or infinite values")
    if raw.min() < -1e-6 or raw.max() > 1 + 1e-6:
        raise ValueError(f"PC outside expected [0,1]: {raw.min()}, {raw.max()}")
    return np.clip(raw, 0, 1).astype(np.float32)


def overlay(base: Image.Image, response: np.ndarray, color: tuple[int, int, int],
            opacity: float = 0.65) -> Image.Image:
    rgb = np.asarray(base.convert("RGB"), dtype=np.float32).copy()
    weight = np.clip(response, 0, 1).astype(np.float32)[..., None] * opacity
    output = rgb * (1 - weight) + np.asarray(color, dtype=np.float32) * weight
    return Image.fromarray(np.uint8(np.clip(np.rint(output), 0, 255)))


def mask_overlay(base: Image.Image, mask: np.ndarray,
                 color: tuple[int, int, int]) -> Image.Image:
    return overlay(base, mask.astype(np.float32), color, 0.55)


def panel_sheet(views: list[tuple[str, Image.Image]], target: Path) -> None:
    """Fixed-size labeled 4x2 sheet, preserving each image's aspect ratio."""
    tile_w, tile_h, heading = 300, 240, 28
    sheet = Image.new("RGB", (4 * tile_w, 2 * (tile_h + heading)), "white")
    draw = ImageDraw.Draw(sheet)
    for index, (label, img) in enumerate(views):
        col, row = index % 4, index // 4
        left, top = col * tile_w, row * (tile_h + heading)
        draw.text((left + 8, top + 7), label, fill="black")
        preview = ImageOps.contain(img.convert("RGB"), (tile_w, tile_h), Image.Resampling.BILINEAR)
        x = left + (tile_w - preview.width) // 2
        y = top + heading + (tile_h - preview.height) // 2
        sheet.paste(preview, (x, y))
    target.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(target)


def analyze_one(image_id: int, data: Path, out: Path, preview: bool,
                save_npy: bool) -> dict:
    image_path = data / "datasets/imgs/pascal" / f"{image_id}.jpg"
    sal_path = data / "pred_saliency" / f"{image_id}_saliency.png"
    gt_path = data / "datasets/masks/pascal" / f"{image_id}.png"
    gaze_path = data / "algmaps/pascal/humanFix" / f"{image_id}.png"
    for path in (image_path, sal_path, gt_path, gaze_path):
        if not path.is_file():
            raise FileNotFoundError(f"Image {image_id}: missing input {path}")

    original = read_rgb(image_path)
    gray = np.asarray(original.convert("L"), dtype=np.uint8)
    sal = read_gray(sal_path)
    gt = read_gray(gt_path)
    gaze = read_gray(gaze_path)
    check_shape(image_id, original=gray, saliency=sal, gt=gt, gaze=gaze)
    s = sal.astype(np.float32) / 255.0
    gt_mask = gt >= 128  # Same PASCAL-S convention as evaluate_pascal_s.py
    roi = sal >= round(0.50 * 255)
    pc = compute_pc(gray)

    pc_gt = mass_fraction(pc, gt_mask)
    pc_roi = mass_fraction(pc, roi)
    area_gt = float(gt_mask.mean())
    area_roi = float(roi.mean())
    hot10 = hot_region(gaze, 0.10)
    pc_gaze = mass_fraction(pc, hot10) if hot10 is not None else None

    maps = out / "pc_maps"
    maps.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.uint16(np.rint(pc * 65535))).save(
        maps / f"{image_id}_pc16.png"
    )
    Image.fromarray(np.uint8(np.rint(pc * 255))).save(
        maps / f"{image_id}_pc.png"
    )
    if save_npy:
        arrays = out / "pc_arrays"
        arrays.mkdir(parents=True, exist_ok=True)
        np.save(arrays / f"{image_id}_pc.npy", pc, allow_pickle=False)

    if preview:
        joint = s * pc  # Diagnostic only: not a validated fusion method
        def mono(x: np.ndarray) -> Image.Image:
            return Image.fromarray(np.uint8(np.rint(np.clip(x, 0, 1) * 255)))
        gaze_img = Image.fromarray(gaze)
        views = [
            ("Original", original),
            ("U2Net saliency", mono(s)),
            ("Phase congruency", mono(pc)),
            ("S * PC (exploratory)", mono(joint)),
            ("Human object GT", mask_overlay(original, gt_mask, (0, 185, 0))),
            ("Human fixation heat", gaze_img),
            ("PC on original", overlay(original, pc, (255, 0, 0))),
            ("U2Net ROI (0.50)", mask_overlay(original, roi, (255, 0, 0))),
        ]
        panel_sheet(views, out / "examples" / f"image_{image_id:03d}.png")

    return {
        "image_id": image_id,
        "width": original.width, "height": original.height,
        "gt_area_fraction": area_gt,
        "roi_area_fraction": area_roi,
        "pc_mean": float(np.mean(pc)),
        "pc_mass_in_gt": pc_gt,
        "pc_mass_in_roi": pc_roi,
        "pc_mass_in_gaze_top10": pc_gaze,
        "pc_enrichment_gt": enrichment(pc_gt, area_gt),
        "pc_enrichment_roi": enrichment(pc_roi, area_roi),
        "saliency_mass_in_gt": mass_fraction(s, gt_mask),
    }


def summarize(rows: list[dict]) -> list[dict]:
    output = []
    for key in rows[0]:
        if key in {"image_id", "width", "height"}:
            continue
        values = np.asarray([row[key] for row in rows if row[key] is not None],
                            dtype=np.float64)
        output.append({
            "metric": key, "valid_images": len(values),
            "mean": float(values.mean()) if len(values) else None,
            "median": float(np.median(values)) if len(values) else None,
            "min": float(values.min()) if len(values) else None,
            "max": float(values.max()) if len(values) else None,
            "p10": float(np.percentile(values, 10)) if len(values) else None,
            "p90": float(np.percentile(values, 90)) if len(values) else None,
        })
    return output


def csv_write(path: Path, rows: list[dict]) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows({k: "" if v is None else v for k, v in row.items()}
                         for row in rows)


def run(args: argparse.Namespace) -> None:
    ids = select_ids(args.split, args.count, args.seed)
    if args.preview_count < 0:
        raise ValueError("--preview-count must be nonnegative")
    data = args.data.expanduser().resolve()
    out = args.output.expanduser().resolve()
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        raise FileExistsError(f"Output is not an empty directory: {out}")
    # Fail before creating results if any of the data files are missing.
    required = (
        ("datasets/imgs/pascal", ".jpg"),
        ("pred_saliency", "_saliency.png"),
        ("datasets/masks/pascal", ".png"),
        ("algmaps/pascal/humanFix", ".png"),
    )
    for image_id in ids:
        for folder, suffix in required:
            path = data / folder / f"{image_id}{suffix}"
            if not path.is_file():
                raise FileNotFoundError(path)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for index, image_id in enumerate(ids, 1):
        record = analyze_one(image_id, data, out,
                             preview=index <= args.preview_count,
                             save_npy=args.save_npy)
        rows.append(record)
        print(f"[{index}/{len(ids)}] image {image_id}: "
              f"PC-in-GT={record['pc_mass_in_gt']}, "
              f"PC-in-ROI={record['pc_mass_in_roi']}", flush=True)

    csv_write(out / "per_image.csv", rows)
    csv_write(out / "summary.csv", summarize(rows))
    try:
        pc_version = version("phasecongruency")
    except PackageNotFoundError:
        pc_version = "unknown"
    (out / "protocol.json").write_text(
        json.dumps({
            "experiment": "PC descriptive pilot (NOT fixed-rectangle selection)",
            "data": str(data), "split": args.split, "count": len(ids),
            "seed": args.seed, "selected_ids": ids,
            "split_ids": split_ids(args.seed),
            "pc_method": "phasecongmono", "pc_version": pc_version,
            "pc_params": {**PC_PARAMS, "backend": "numpy"},
            "pc_computed_at": "native image dimensions, grayscale L",
            "pc_normalization": "none; clip only numerical [0,1] excursions",
            "u2net": "saved 8-bit continuous saliency (no re-inference)",
            "gt_threshold_byte": 128, "roi_threshold_byte": round(0.5 * 255),
            "gaze_top10": "existing hot_region policy; undefined allowed",
            "joint_S_times_PC": "preview only, not a region selection formula",
            "ratios": "fractions 0..1; enrichment = mass_fraction / area_fraction",
            "pc16": "round(PC * 65535) for persistence",
            "historical_data_modified": False,
        }, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(f"Done. Results: {out}")


def main() -> None:
    p = argparse.ArgumentParser(description="PC exploratory experiment using saved PASCAL-S U2Net maps")
    p.add_argument("--data", type=Path, default=DATA_DEFAULT)
    p.add_argument("--output", type=Path, default=Path("output/pc_pilot_20"))
    p.add_argument("--split", choices=["train", "val", "test", "all"], default="train")
    p.add_argument("--count", type=int, default=20)
    p.add_argument("--seed", type=int, default=2026)
    p.add_argument("--preview-count", type=int, default=20)
    p.add_argument("--save-npy", action="store_true",
                   help="Also save exact float32 PC arrays (larger disk use)")
    run(p.parse_args())


if __name__ == "__main__":
    main()

