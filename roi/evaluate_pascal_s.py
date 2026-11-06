#!/usr/bin/env python3
"""Evaluate existing U2-Net ROI masks against PASCAL-S object and gaze annotations.

No U2-Net inference, ROI optimization, model training or hardware modification.
All rate fields written to CSV are fractions in [0, 1], not percentages.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

TOP_FRACTIONS = (0.05, 0.10, 0.20)
ROOT_DEFAULT = Path.home() / "datasets" / "PASCAL-S"


def read_gray(path: Path) -> np.ndarray:
    with Image.open(path) as image:
        return np.asarray(image.convert("L"), dtype=np.uint8)


def read_rgb(path: Path) -> Image.Image:
    with Image.open(path) as image:
        return image.convert("RGB")


def check_shape(image_id: int, **arrays: np.ndarray) -> None:
    shapes = {label: arr.shape[:2] for label, arr in arrays.items()}
    if len(set(shapes.values())) != 1:
        raise ValueError(f"Image {image_id}: dimension mismatch {shapes}; no implicit resize allowed")


def safe_div(numerator: float, denominator: float) -> float | None:
    return float(numerator / denominator) if denominator > 0 else None


def mask_scores(pred: np.ndarray, truth: np.ndarray) -> dict[str, float | None]:
    """Binary mask metrics; undefined empty denominators remain null, never 100%."""
    intersection = int(np.count_nonzero(pred & truth))
    pred_n = int(np.count_nonzero(pred))
    truth_n = int(np.count_nonzero(truth))
    union = pred_n + truth_n - intersection
    return {
        "iou": safe_div(intersection, union),
        "precision": safe_div(intersection, pred_n),
        "recall": safe_div(intersection, truth_n),
        "roi_area_fraction": pred_n / pred.size,
        "gt_area_fraction": truth_n / truth.size,
    }


def hot_region(heatmap: np.ndarray, top_fraction: float) -> np.ndarray | None:
    """Top q% of ALL image pixels, including all positive-value threshold ties.

    If the map has fewer than q% positive-valued pixels, top-q is undefined
    rather than arbitrarily selecting zero-valued background.
    """
    if heatmap.ndim != 2 or not 0 < top_fraction < 1:
        raise ValueError("Expected a 2D heatmap and top fraction between 0 and 1")
    values = heatmap.ravel()
    k = max(1, math.ceil(values.size * top_fraction))
    if np.count_nonzero(values) < k:
        return None
    threshold = np.partition(values, values.size - k)[values.size - k]
    if threshold == 0:
        return None
    return heatmap >= threshold


def region_gaze_metrics(pred: np.ndarray, heatmap: np.ndarray) -> dict[str, float | None]:
    result: dict[str, float | None] = {
        "heatmap_mass_coverage": safe_div(float(heatmap[pred].sum()), float(heatmap.sum()))
    }
    for top in TOP_FRACTIONS:
        suffix = f"top{int(top * 100):02d}"
        hot = hot_region(heatmap, top)
        result[f"{suffix}_actual_area_fraction"] = (float(hot.mean()) if hot is not None else None)
        result[f"{suffix}_roi_hit"] = (
            safe_div(int(np.count_nonzero(pred & hot)), int(np.count_nonzero(pred)))
            if hot is not None else None
        )
    return result


class FixationReader:
    """PASCAL-S original salObj.zip MATLAB v7.3 fixCell (1-based [row, col])."""

    def __init__(self, mat_path: Path, expected_images: int = 850):
        try:
            import h5py
        except ImportError as exc:
            raise RuntimeError("Reading pascalFix.mat requires 'pip install h5py'") from exc
        self._file = h5py.File(mat_path, "r")
        if "fixCell" not in self._file:
            self._file.close()
            raise ValueError("pascalFix.mat has no 'fixCell' dataset; inspect its MATLAB structure")
        self._cells = self._file["fixCell"]
        if self._cells.size != expected_images:
            self._file.close()
            raise ValueError(f"Expected {expected_images} fixCell entries, got {self._cells.size}")

    def read(self, image_id: int, size: tuple[int, int]) -> tuple[np.ndarray, np.ndarray]:
        width, height = size
        reference = self._cells[image_id - 1, 0] if self._cells.shape[0] == 850 else self._cells[0, image_id - 1]
        array = np.asarray(self._file[reference], dtype=np.float64)
        # PASCAL-S salObj.zip stores fixation positions in the first two
        # rows ([row, column], MATLAB 1-based); some entries also contain a
        # third auxiliary row. This is NOT another coordinate axis.
        if array.ndim != 2:
            raise ValueError(f"Image {image_id}: expected 2D fixation data, got {array.shape}")
        if array.shape[0] in (2, 3):
            coords = array[:2, :].T
        elif array.shape[1] in (2, 3):
            coords = array[:, :2]
        else:
            raise ValueError(f"Image {image_id}: expected 2xN/3xN or Nx2/Nx3, got {array.shape}")
        if coords.size == 0:
            return np.array([], dtype=np.int64), np.array([], dtype=np.int64)
        # PASCAL-S MATLAB indexing: [row, column], 1-based.
        if not np.all(np.isfinite(coords)):
            raise ValueError(f"Image {image_id}: nonfinite fixation coordinates")
        rows = coords[:, 0].astype(np.int64) - 1
        cols = coords[:, 1].astype(np.int64) - 1
        if np.any(rows < 0) or np.any(rows >= height) or np.any(cols < 0) or np.any(cols >= width):
            raise ValueError(f"Image {image_id}: fixation coordinates out of image bounds {size}; inspect MAT orientation")
        return rows, cols

    def close(self) -> None:
        self._file.close()

    def __enter__(self) -> "FixationReader":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()


def measure(image_id: int, root: Path, gt_threshold: int, fixations: FixationReader | None,
            pred_dir: Path | None = None) -> dict:
    img_path = root / "datasets/imgs/pascal" / f"{image_id}.jpg"
    gt_path = root / "datasets/masks/pascal" / f"{image_id}.png"
    pred_path = (pred_dir if pred_dir is not None else root / "pred_masks") / f"{image_id}_mask.png"
    heat_path = root / "algmaps/pascal/humanFix" / f"{image_id}.png"
    for path in (img_path, gt_path, pred_path, heat_path):
        if not path.is_file():
            raise FileNotFoundError(f"Image {image_id}: missing {path}")
    with Image.open(img_path) as src:
        size = src.size
    gt = read_gray(gt_path)
    pred = read_gray(pred_path)
    heat = read_gray(heat_path)
    check_shape(image_id, original=np.zeros((size[1], size[0]), dtype=np.uint8), gt=gt, pred=pred, fixation_heatmap=heat)
    gt_bin, pred_bin = gt >= gt_threshold, pred >= 128
    row: dict = {"image_id": image_id, "image_width": size[0], "image_height": size[1]}
    row.update(mask_scores(pred_bin, gt_bin))
    row.update(region_gaze_metrics(pred_bin, heat))
    row["fixation_count"] = None
    row["fixation_point_coverage"] = None
    if fixations is not None:
        rr, cc = fixations.read(image_id, size)
        row["fixation_count"] = int(len(rr))
        row["fixation_point_coverage"] = float(np.mean(pred_bin[rr, cc])) if len(rr) else None
    return row


def mix(original: Image.Image, binary: np.ndarray, color: tuple[int, int, int], alpha: float = 0.58) -> Image.Image:
    base = np.asarray(original.convert("RGB"), dtype=np.float32).copy()
    base[binary] = (1 - alpha) * base[binary] + alpha * np.array(color)
    return Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))


def preview(image_id: int, root: Path, output_path: Path,
            pred_dir: Path | None = None) -> None:
    original = read_rgb(root / "datasets/imgs/pascal" / f"{image_id}.jpg")
    gt = read_gray(root / "datasets/masks/pascal" / f"{image_id}.png") >= 128
    pred = read_gray((pred_dir if pred_dir is not None else root / "pred_masks") / f"{image_id}_mask.png") >= 128
    heat = read_gray(root / "algmaps/pascal/humanFix" / f"{image_id}.png")
    hot = hot_region(heat, 0.10)
    if hot is None:
        hot = np.zeros_like(pred)
    overlay = mix(original, hot, (0, 0, 255), alpha=0.48)
    overlay = mix(overlay, pred & ~hot, (255, 0, 0), alpha=0.75)
    overlay = mix(overlay, pred & hot, (255, 255, 0), alpha=0.85)
    views = [original, mix(original, gt, (0, 255, 0)), mix(original, pred, (255, 0, 0)), overlay]
    titles = ["Original", "PASCAL-S human object GT", "U2-Net ROI", "Top10% attention: blue / overlap: yellow"]
    w, h = original.size
    sheet = Image.new("RGB", (2 * w, 2 * (h + 32)), "white")
    draw = ImageDraw.Draw(sheet)
    for idx, (im, title) in enumerate(zip(views, titles)):
        x, y = (idx % 2) * w, (idx // 2) * (h + 32)
        draw.text((x + 8, y + 8), title, fill=(0, 0, 0))
        sheet.paste(im, (x, y + 32))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output_path)


def stats(rows: list[dict], headers: list[str]) -> list[dict]:
    result = []
    for key in headers:
        if key in {"image_id", "image_width", "image_height", "fixation_count"}:
            continue
        values = [float(row[key]) for row in rows if row[key] is not None]
        result.append({"metric": key, "valid_images": len(values),
                       "mean": statistics.mean(values) if values else None,
                       "median": statistics.median(values) if values else None,
                       "min": min(values) if values else None,
                       "max": max(values) if values else None,
                       "p10": float(np.percentile(values, 10)) if values else None,
                       "p90": float(np.percentile(values, 90)) if values else None})
    return result


def write_csv(path: Path, data: list[dict], columns: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as fh:
        writer = csv.DictWriter(fh, fieldnames=columns)
        writer.writeheader()
        for row in data:
            writer.writerow({key: "" if row.get(key) is None else row.get(key) for key in columns})


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Evaluate existing PASCAL-S U2-Net ROI against human labels and gaze")
    p.add_argument("--data", type=Path, default=ROOT_DEFAULT, help="PASCAL-S dataset root")
    p.add_argument("--output", type=Path, default=Path("output/pascal_s_evaluation"))
    p.add_argument("--start", type=int, default=1, help="First image ID, inclusive")
    p.add_argument("--count", type=int, default=20, help="Number of sequential image IDs, up to 850")
    p.add_argument("--gt-threshold", type=int, default=128, help="Human object GT >= threshold; default 128")
    p.add_argument("--pred-dir", type=Path, default=Path("pred_masks"),
                   help="Prediction directory; relative paths resolve under --data")
    p.add_argument("--fixations", choices=("mat", "none"), default="mat", help="mat: use actual raw human gaze points; none: heatmap-only dry-run")
    p.add_argument("--preview-count", type=int, default=6, help="Save examples ranked by top10 ROI hit")
    return p.parse_args()


def main() -> None:
    args = parse_args()
    if args.start < 1 or args.count < 1 or args.start + args.count - 1 > 850:
        raise ValueError("--start and --count must select image IDs within 1..850")
    if not 1 <= args.gt_threshold <= 255:
        raise ValueError("--gt-threshold must be in 1..255")
    if args.preview_count < 0:
        raise ValueError("--preview-count must be nonnegative")
    root = args.data.expanduser().resolve()
    out = args.output.expanduser().resolve()
    pred_dir = args.pred_dir.expanduser()
    pred_dir = (pred_dir if pred_dir.is_absolute() else root / pred_dir).resolve()
    if not pred_dir.is_dir():
        raise NotADirectoryError(f"Prediction mask directory not found: {pred_dir}")
    if out.exists() and any(out.iterdir()):
        raise FileExistsError(f"Output folder is not empty: {out}; use a new folder to preserve existing evidence")
    fixation_reader = (FixationReader(root / "datasets/fixations/pascalFix.mat") if args.fixations == "mat" else None)
    try:
        rows = []
        for image_id in range(args.start, args.start + args.count):
            row = measure(image_id, root, args.gt_threshold, fixation_reader, pred_dir=pred_dir)
            rows.append(row)
            print(f"[{len(rows):3d}/{args.count}] image {image_id:3d}: IoU={row['iou']!s}, top10 ROI hit={row['top10_roi_hit']!s}")
    finally:
        if fixation_reader is not None:
            fixation_reader.close()
    out.mkdir(parents=True, exist_ok=True)
    columns = list(rows[0].keys())
    write_csv(out / "per_image.csv", rows, columns)
    summary = stats(rows, columns)
    write_csv(out / "summary.csv", summary, ["metric", "valid_images", "mean", "median", "min", "max", "p10", "p90"])
    settings = {"data_root": str(root), "start": args.start, "count": args.count,
                "gt_threshold": args.gt_threshold, "prediction_threshold": 128,
                "prediction_mask_directory": str(pred_dir),
                "high_attention_top_fractions": TOP_FRACTIONS,
                "high_attention_policy": "Top fraction of ALL image pixels by 8-bit humanFix value; include positive-value ties; N/A if insufficient positive pixels",
                "fixation_source": "pascalFix.mat: fixCell, 1-based [row,col]" if fixation_reader is not None else "not used",
                "units": "all ratio metrics are fractions [0,1]",
                "no_hardware_or_roi_area_constraint": True}
    (out / "protocol.json").write_text(json.dumps(settings, indent=2, ensure_ascii=False), encoding="utf-8")
    ordered = sorted([r for r in rows if r["top10_roi_hit"] is not None], key=lambda x: x["top10_roi_hit"])
    candidates = [r["image_id"] for r in rows[:2]]
    n = args.preview_count
    candidates += [r["image_id"] for r in ordered[:n//2]]
    candidates += [r["image_id"] for r in ordered[-(n - n//2):]] if n else []
    used: set[int] = set()
    for image_id in candidates:
        if image_id not in used:
            used.add(image_id)
            preview(image_id, root, out / "examples" / f"image_{image_id:03d}.png", pred_dir=pred_dir)
    print(f"Done: {len(rows)} images. Reports: {out}")
    print("CSV ratios are 0..1; high-attention hit is overlap / ROI pixels, not attention recall.")


if __name__ == "__main__":
    main()

