#!/usr/bin/env python3
"""Full-dataset statistics of U2-Net salient masks and PC/contrast structure candidates.

This is NOT model training and NOT rectangle-position search. It extracts
per-image masks, accumulates spatial occurrence frequencies, then obtains
consensus connected regions and bounding boxes directly from those maps.
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import label, uniform_filter

from pc_extract import PC_PARAMS, compute_pc

DATA_DEFAULT = Path.home() / "datasets" / "PASCAL-S"
METHODS = ("u2net", "pc", "contrast", "structure", "common", "union")
CONSENSUS_SOURCES = ("u2net", "structure", "common", "union")
SENSITIVITY_THRESHOLDS = (0.10, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80)


def otsu_threshold(values: np.ndarray, bins: int = 256) -> float | None:
    """Return a data-derived Otsu threshold; None means no contrast in values."""
    data = np.asarray(values, dtype=np.float64)
    data = data[np.isfinite(data)]
    if data.size == 0:
        return None
    low, high = float(data.min()), float(data.max())
    if high - low <= 1e-12:
        return None
    hist, edges = np.histogram(data, bins=bins, range=(low, high))
    probability = hist.astype(np.float64) / float(hist.sum())
    centers = (edges[:-1] + edges[1:]) * 0.5
    weight_left = np.cumsum(probability)
    mean_left = np.cumsum(probability * centers)
    mean_total = float(mean_left[-1])
    denominator = weight_left * (1.0 - weight_left)
    between = np.zeros_like(denominator)
    valid = denominator > 1e-15
    between[valid] = (
        mean_total * weight_left[valid] - mean_left[valid]
    ) ** 2 / denominator[valid]
    if not np.any(valid):
        return None
    index = int(np.argmax(between))
    return float((edges[index] + edges[index + 1]) * 0.5)


def otsu_mask(values: np.ndarray) -> tuple[np.ndarray, float | None]:
    """High-response candidate pixels; a constant map yields an empty mask."""
    threshold = otsu_threshold(values)
    if threshold is None:
        return np.zeros(np.asarray(values).shape, dtype=bool), None
    return np.asarray(values, dtype=np.float64) > threshold, threshold


def local_contrast(gray: np.ndarray, window: int = 9) -> np.ndarray:
    if window < 3 or window % 2 == 0:
        raise ValueError("Contrast window must be an odd integer >= 3")
    image = np.asarray(gray, dtype=np.float64) / 255.0
    mean = uniform_filter(image, size=window, mode="reflect")
    mean_sq = uniform_filter(image * image, size=window, mode="reflect")
    return np.sqrt(np.maximum(0.0, mean_sq - mean * mean)).astype(np.float32)


def map_to_canvas(
    array: np.ndarray,
    grid: int = 128,
    coordinate_mode: str = "letterbox",
    is_mask: bool = False,
) -> tuple[np.ndarray, np.ndarray, dict]:
    """Map to a common canvas; letterbox preserves aspect ratio and excludes padding."""
    if coordinate_mode not in {"letterbox", "normalized"}:
        raise ValueError("coordinate_mode must be letterbox or normalized")
    src = np.asarray(array)
    if src.ndim != 2:
        raise ValueError("Expected a 2-D image/map")
    height, width = src.shape
    if height < 1 or width < 1:
        raise ValueError("Empty input map")
    if coordinate_mode == "normalized":
        out_w = out_h = grid
    else:
        scale = min(grid / width, grid / height)
        out_w = max(1, min(grid, int(round(width * scale))))
        out_h = max(1, min(grid, int(round(height * scale))))
    left = (grid - out_w) // 2
    top = (grid - out_h) // 2
    resample = Image.Resampling.NEAREST if is_mask else Image.Resampling.BILINEAR
    pil = Image.fromarray(src.astype(np.float32))
    resized = pil.resize((out_w, out_h), resample=resample)
    canvas = np.zeros((grid, grid), dtype=np.float32)
    canvas[top:top + out_h, left:left + out_w] = np.asarray(resized, dtype=np.float32)
    valid = np.zeros((grid, grid), dtype=bool)
    valid[top:top + out_h, left:left + out_w] = True
    transform = {
        "source_width": int(width), "source_height": int(height),
        "left": int(left), "top": int(top),
        "canvas_width": int(out_w), "canvas_height": int(out_h),
        "grid": int(grid), "coordinate_mode": coordinate_mode,
    }
    return canvas, valid, transform


def read_u2_saliency(path: Path, expected_size: tuple[int, int]) -> np.ndarray:
    with Image.open(path) as image:
        if image.size != expected_size:
            raise ValueError(f"U2-Net saliency size {image.size} != image size {expected_size}: {path}")
        return np.asarray(image.convert("L"), dtype=np.float32) / 255.0


def read_cached_pc(path: Path, expected_size: tuple[int, int]) -> np.ndarray:
    with Image.open(path) as image:
        if image.size != expected_size:
            raise ValueError(f"PC map size {image.size} != image size {expected_size}: {path}")
        raw = np.asarray(image)
    if raw.dtype == np.uint16 or (raw.size and float(raw.max()) > 255):
        result = raw.astype(np.float32) / 65535.0
    else:
        result = raw.astype(np.float32) / 255.0
    return np.clip(result, 0.0, 1.0)


def make_masks(
    saliency: np.ndarray,
    pc: np.ndarray,
    contrast: np.ndarray,
    u2_threshold: float,
) -> tuple[dict[str, np.ndarray], dict[str, float | None]]:
    """Create feature masks and conservative combinations.

    PC and local contrast are structural evidence, not a semantic saliency
    detector. Their intersection is therefore called 'structure', not GT.
    """
    u2_mask = saliency >= u2_threshold
    pc_mask, pc_threshold = otsu_mask(pc)
    contrast_mask, contrast_threshold = otsu_mask(contrast)
    structure = pc_mask & contrast_mask
    common = u2_mask & structure
    union = u2_mask | structure
    masks = {
        "u2net": u2_mask,
        "pc": pc_mask,
        "contrast": contrast_mask,
        "structure": structure,
        "common": common,
        "union": union,
    }
    thresholds = {"pc_otsu": pc_threshold, "contrast_otsu": contrast_threshold}
    return masks, thresholds


def mask_agreement(masks: dict[str, np.ndarray]) -> dict:
    u2 = masks["u2net"]
    structure = masks["structure"]
    common = masks["common"]
    union = masks["union"]
    u2_n, structure_n = int(u2.sum()), int(structure.sum())
    common_n, union_n = int(common.sum()), int(union.sum())
    return {
        "u2_area_fraction": float(u2.mean()),
        "pc_area_fraction": float(masks["pc"].mean()),
        "contrast_area_fraction": float(masks["contrast"].mean()),
        "structure_area_fraction": float(structure.mean()),
        "common_area_fraction": float(common.mean()),
        "union_area_fraction": float(union.mean()),
        "common_over_u2": common_n / u2_n if u2_n else None,
        "common_over_structure": common_n / structure_n if structure_n else None,
        "u2_structure_iou": common_n / union_n if union_n else None,
    }


def update_accumulators(
    masks: dict[str, np.ndarray],
    feature_maps: dict[str, np.ndarray],
    valid: np.ndarray,
    mask_counts: dict[str, np.ndarray],
    feature_sums: dict[str, np.ndarray],
    valid_counts: np.ndarray,
) -> None:
    valid_counts += valid.astype(np.uint32)
    for name, mask in masks.items():
        mask_counts[name] += (mask & valid).astype(np.uint32)
    for name, values in feature_maps.items():
        feature_sums[name] += np.asarray(values, dtype=np.float64) * valid


def frequency_map(counts: np.ndarray, valid_counts: np.ndarray) -> np.ndarray:
    result = np.zeros(counts.shape, dtype=np.float32)
    np.divide(counts, valid_counts, out=result, where=valid_counts > 0)
    return result


def component_records(mask: np.ndarray) -> tuple[list[dict], list[int] | None]:
    """Return 8-connected component boxes and the bbox of all consensus pixels."""
    binary = np.asarray(mask, dtype=bool)
    ys, xs = np.where(binary)
    if len(xs) == 0:
        return [], None
    global_box = [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1]
    labels, count = label(binary, structure=np.ones((3, 3), dtype=np.uint8))
    records = []
    for component_id in range(1, count + 1):
        cy, cx = np.where(labels == component_id)
        x0, x1 = int(cx.min()), int(cx.max()) + 1
        y0, y1 = int(cy.min()), int(cy.max()) + 1
        component_area = int(len(cx))
        bbox_area = (x1 - x0) * (y1 - y0)
        records.append({
            "component_id": component_id,
            "x0": x0, "y0": y0, "x1": x1, "y1": y1,
            "pixel_count": component_area,
            "bbox_area": bbox_area,
            "bbox_fill_ratio": component_area / bbox_area if bbox_area else None,
        })
    records.sort(key=lambda row: (-row["pixel_count"], row["component_id"]))
    return records, global_box


def write_csv(path: Path, rows: list[dict]) -> None:
    if not rows:
        raise ValueError(f"No rows to write: {path}")
    with path.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows({key: "" if value is None else value for key, value in row.items()}
                         for row in rows)


def save_gray(path: Path, values: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    scaled = np.uint8(np.rint(np.clip(values, 0.0, 1.0) * 255.0))
    Image.fromarray(scaled).save(path)


def save_mask(path: Path, mask: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.asarray(mask, dtype=np.uint8) * 255).save(path)


def map_box_to_original(box: list[int], transform: dict) -> tuple[int, int, int, int]:
    x0, y0, x1, y1 = box
    left, top = transform["left"], transform["top"]
    cw, ch = transform["canvas_width"], transform["canvas_height"]
    width, height = transform["source_width"], transform["source_height"]
    sx, sy = width / cw, height / ch
    ox0 = int(round((x0 - left) * sx))
    oy0 = int(round((y0 - top) * sy))
    ox1 = int(round((x1 - left) * sx))
    oy1 = int(round((y1 - top) * sy))
    return (max(0, min(width, ox0)), max(0, min(height, oy0)),
            max(0, min(width, ox1)), max(0, min(height, oy1)))


def draw_heatmap_box(
    frequency: np.ndarray,
    box: list[int] | None,
    components: list[dict],
    path: Path,
) -> None:
    gray = np.uint8(np.rint(np.clip(frequency, 0, 1) * 255))
    base = Image.fromarray(gray).convert("RGB").resize((768, 768), Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(base)
    scale = 768.0 / frequency.shape[1]
    for item in components:
        x0, y0, x1, y1 = (int(round(v * scale)) for v in
                           (item["x0"], item["y0"], item["x1"], item["y1"]))
        draw.rectangle((x0, y0, max(x0, x1 - 1), max(y0, y1 - 1)), outline=(255, 220, 0), width=3)
    if box:
        x0, y0, x1, y1 = (int(round(v * scale)) for v in box)
        draw.rectangle((x0, y0, max(x0, x1 - 1), max(y0, y1 - 1)), outline=(255, 0, 0), width=5)
    path.parent.mkdir(parents=True, exist_ok=True)
    base.save(path)


def overlay_on_image(
    image_path: Path,
    box: list[int] | None,
    transform: dict,
    output_path: Path,
) -> None:
    with Image.open(image_path) as source:
        image = source.convert("RGB")
    if box:
        rect = map_box_to_original(box, transform)
        ImageDraw.Draw(image).rectangle(rect, outline=(255, 0, 0), width=max(2, image.width // 250))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path)


def run(args: argparse.Namespace) -> None:
    data = args.data.expanduser().resolve()
    out = args.output.expanduser().resolve()
    pc_dir = args.pc_dir.expanduser().resolve() if args.pc_dir else None
    ids = list(range(args.start, args.start + args.count))
    if args.start < 1 or args.count < 1 or ids[-1] > 850:
        raise ValueError("Select image IDs within 1..850")
    if not 0 < args.u2_threshold < 1:
        raise ValueError("--u2-threshold must be between 0 and 1")
    if args.grid < 32:
        raise ValueError("--grid must be >= 32")
    if args.contrast_window < 3 or args.contrast_window % 2 == 0:
        raise ValueError("--contrast-window must be odd and >= 3")
    if args.preview_count < 0:
        raise ValueError("--preview-count must be nonnegative")
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        raise FileExistsError(f"Output directory must be new or empty: {out}")

    # Validate all image and saved U2-Net inputs before starting.
    for image_id in ids:
        image_path = data / "datasets/imgs/pascal" / f"{image_id}.jpg"
        sal_path = data / "pred_saliency" / f"{image_id}_saliency.png"
        if not image_path.is_file():
            raise FileNotFoundError(image_path)
        if not sal_path.is_file():
            raise FileNotFoundError(sal_path)

    out.mkdir(parents=True, exist_ok=True)
    mask_counts = {name: np.zeros((args.grid, args.grid), dtype=np.uint32) for name in METHODS}
    feature_sums = {name: np.zeros((args.grid, args.grid), dtype=np.float64)
                    for name in ("u2net", "pc", "contrast")}
    valid_counts = np.zeros((args.grid, args.grid), dtype=np.uint32)
    per_image_rows = []
    transforms = {}
    mask_root = out / "per_image_masks"
    feature_root = out / "per_image_features"
    computed_pc_dir = feature_root / "pc16_computed"
    image_root = data / "datasets/imgs/pascal"
    sal_root = data / "pred_saliency"

    for index, image_id in enumerate(ids, 1):
        image_path = image_root / f"{image_id}.jpg"
        sal_path = sal_root / f"{image_id}_saliency.png"
        with Image.open(image_path) as source:
            original = source.convert("RGB")
        gray = np.asarray(original.convert("L"), dtype=np.uint8)
        width, height = original.size
        saliency = read_u2_saliency(sal_path, (width, height))

        pc_path = pc_dir / f"{image_id}_pc16.png" if pc_dir else None
        if pc_path is not None and pc_path.is_file():
            pc = read_cached_pc(pc_path, (width, height))
            pc_source = "cached"
        else:
            pc = compute_pc(gray)
            pc_source = "computed"
            computed_pc_dir.mkdir(parents=True, exist_ok=True)
            Image.fromarray(np.uint16(np.rint(pc * 65535))).save(
                computed_pc_dir / f"{image_id}_pc16.png"
            )
        contrast = local_contrast(gray, args.contrast_window)
        masks, thresholds = make_masks(saliency, pc, contrast, args.u2_threshold)

        mapped_masks = {}
        valid = None
        transform = None
        for name, mask in masks.items():
            mapped, current_valid, current_transform = map_to_canvas(
                mask.astype(np.float32), args.grid, args.coordinate_mode, is_mask=True
            )
            mapped_masks[name] = mapped >= 0.5
            if valid is None:
                valid, transform = current_valid, current_transform
            elif not np.array_equal(valid, current_valid):
                raise RuntimeError("Coordinate transforms produced inconsistent valid regions")
        assert valid is not None and transform is not None

        mapped_features = {}
        for name, values in (("u2net", saliency), ("pc", pc), ("contrast", contrast)):
            mapped, feature_valid, _ = map_to_canvas(
                values, args.grid, args.coordinate_mode, is_mask=False
            )
            if not np.array_equal(valid, feature_valid):
                raise RuntimeError("Feature/mask valid regions differ")
            mapped_features[name] = mapped

        update_accumulators(mapped_masks, mapped_features, valid,
                            mask_counts, feature_sums, valid_counts)
        transforms[image_id] = transform

        for name, mask in mapped_masks.items():
            save_mask(mask_root / name / f"{image_id}.png", mask)
        for name, values in mapped_features.items():
            save_gray(feature_root / name / f"{image_id}.png", values)

        metrics = mask_agreement(masks)
        per_image_rows.append({
            "image_id": image_id, "width": width, "height": height,
            "aspect_ratio": width / height,
            "pc_source": pc_source,
            "pc_otsu_threshold": thresholds["pc_otsu"],
            "contrast_otsu_threshold": thresholds["contrast_otsu"],
            **metrics,
        })
        if index % 25 == 0 or index == len(ids):
            print(f"[{index}/{len(ids)}] processed image {image_id}", flush=True)

    frequency = {name: frequency_map(mask_counts[name], valid_counts) for name in METHODS}
    means = {}
    for name, total in feature_sums.items():
        mean = np.zeros(total.shape, dtype=np.float32)
        np.divide(total, valid_counts, out=mean, where=valid_counts > 0)
        means[name] = mean

    freq_dir = out / "spatial_frequency"
    freq_dir.mkdir(parents=True, exist_ok=True)
    for name, values in frequency.items():
        np.save(freq_dir / f"{name}_frequency.npy", values, allow_pickle=False)
        save_gray(freq_dir / f"{name}_frequency.png", values)
    for name, values in means.items():
        np.save(freq_dir / f"{name}_mean.npy", values, allow_pickle=False)
        save_gray(freq_dir / f"{name}_mean.png", values)
    np.save(freq_dir / "valid_image_count.npy", valid_counts, allow_pickle=False)
    save_gray(freq_dir / "valid_image_count.png", valid_counts / max(1, len(ids)))

    consensus_dir = out / "consensus"
    consensus_dir.mkdir(parents=True, exist_ok=True)
    component_rows = []
    rectangle_summary = []
    main_box = None
    main_components = []
    main_threshold = None
    for source_name in CONSENSUS_SOURCES:
        values = frequency[source_name]
        valid_values = values[valid_counts > 0]
        threshold = otsu_threshold(valid_values)
        consensus = ((values > threshold) & (valid_counts > 0)
                     if threshold is not None else np.zeros(values.shape, dtype=bool))
        components, bbox = component_records(consensus)
        save_mask(consensus_dir / f"{source_name}_otsu_mask.png", consensus)
        np.save(consensus_dir / f"{source_name}_otsu_mask.npy", consensus, allow_pickle=False)
        draw_heatmap_box(values, bbox, components,
                         consensus_dir / f"{source_name}_bbox_overlay.png")
        rectangle_summary.append({
            "source": source_name, "otsu_threshold": threshold,
            "consensus_pixel_fraction": float(consensus.mean()),
            "component_count": len(components),
            "global_bbox_xyxy": json.dumps(bbox) if bbox else "",
            "bbox_area_fraction": ((bbox[2] - bbox[0]) * (bbox[3] - bbox[1]) /
                                   float(args.grid * args.grid)) if bbox else None,
            "consensus_fill_ratio": (float(consensus.sum()) /
                                     ((bbox[2] - bbox[0]) * (bbox[3] - bbox[1]))
                                     if bbox else None),
        })
        for item in components:
            component_rows.append({"source": source_name, **item})
        if source_name == "common":
            main_box, main_components, main_threshold = bbox, components, threshold

    sensitivity_rows = []
    common_frequency = frequency["common"]
    for threshold in SENSITIVITY_THRESHOLDS:
        mask = (common_frequency >= threshold) & (valid_counts > 0)
        components, bbox = component_records(mask)
        sensitivity_rows.append({
            "frequency_threshold": threshold,
            "consensus_pixel_fraction": float(mask.mean()),
            "component_count": len(components),
            "global_bbox_xyxy": json.dumps(bbox) if bbox else "",
            "bbox_area_fraction": ((bbox[2] - bbox[0]) * (bbox[3] - bbox[1]) /
                                   float(args.grid * args.grid)) if bbox else None,
            "consensus_fill_ratio": (float(mask.sum()) /
                                     ((bbox[2] - bbox[0]) * (bbox[3] - bbox[1]))
                                     if bbox else None),
        })
        save_mask(consensus_dir / f"common_frequency_ge_{threshold:.2f}.png", mask)

    if main_box:
        for image_id in ids[:args.preview_count]:
            overlay_on_image(
                image_root / f"{image_id}.jpg", main_box, transforms[image_id],
                out / "rectangle_overlays" / f"{image_id}.png"
            )

    write_csv(out / "per_image_statistics.csv", per_image_rows)
    write_csv(out / "consensus_rectangles.csv", rectangle_summary)
    write_csv(out / "consensus_components.csv", component_rows or [
        {"source": "", "component_id": "", "x0": "", "y0": "", "x1": "",
         "y1": "", "pixel_count": "", "bbox_area": "", "bbox_fill_ratio": ""}
    ])
    write_csv(out / "common_threshold_sensitivity.csv", sensitivity_rows)

    protocol = {
        "experiment": "full-dataset per-image region extraction and spatial consensus",
        "images_processed": len(ids), "image_ids": ids,
        "u2net": "existing saved saliency map; no inference or training",
        "u2_threshold": args.u2_threshold,
        "pc": "phasecongmono from pc_extract.py; cached 16-bit maps reused when available",
        "pc_parameters": PC_PARAMS,
        "pc_threshold": "per-image Otsu",
        "local_contrast": f"local grayscale standard deviation, {args.contrast_window}x{args.contrast_window}",
        "contrast_threshold": "per-image Otsu",
        "structure_candidate": "Otsu(PC) AND Otsu(local contrast); structural evidence, not a semantic saliency detector",
        "common_region": "U2-Net binary mask INTERSECT structural candidate mask",
        "union_region": "U2-Net binary mask UNION structural candidate mask",
        "coordinate_mode": args.coordinate_mode,
        "coordinate_note": ("aspect-ratio-preserving letterbox; padded pixels excluded from per-cell denominators"
                            if args.coordinate_mode == "letterbox" else
                            "independent x/y normalization to a unit square; geometry is stretched"),
        "spatial_statistics": "per-cell image occurrence frequency, denominator is number of images valid at that cell",
        "consensus_rule": "Otsu threshold on spatial frequency; 8-connected components; direct bounding boxes",
        "rectangle_search": False,
        "model_training": False,
        "human_gt_or_gaze_used_for_selection": False,
        "main_candidate_source": "common",
        "main_candidate_bbox_xyxy_on_grid": main_box,
        "main_candidate_otsu_frequency_threshold": main_threshold,
        "multiple_components_are_reported_not_silently_merged": True,
        "note": "A global bbox around disconnected components may include gaps; inspect component count and fill ratio before claiming one coherent fixed region.",
    }
    (out / "protocol.json").write_text(json.dumps(protocol, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Done. Spatial statistics and consensus regions: {out}")
    if main_box is None:
        print("WARNING: no common consensus region was found under Otsu threshold.")
    elif len(main_components) > 1:
        print(f"WARNING: common consensus contains {len(main_components)} disconnected components; inspect component CSV.")
    else:
        print(f"Common-region bbox on {args.grid}x{args.grid} grid: {main_box}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=DATA_DEFAULT)
    parser.add_argument("--output", type=Path, default=Path("output/consensus_region_statistics"))
    parser.add_argument("--pc-dir", type=Path, default=Path("output/pc_train_600/pc_maps"),
                        help="Optional cache with <id>_pc16.png; missing IDs are computed")
    parser.add_argument("--start", type=int, default=1)
    parser.add_argument("--count", type=int, default=850)
    parser.add_argument("--grid", type=int, default=128)
    parser.add_argument("--coordinate-mode", choices=("letterbox", "normalized"), default="letterbox")
    parser.add_argument("--u2-threshold", type=float, default=0.50)
    parser.add_argument("--contrast-window", type=int, default=9)
    parser.add_argument("--preview-count", type=int, default=12)
    return parser.parse_args()


if __name__ == "__main__":
    run(parse_args())

