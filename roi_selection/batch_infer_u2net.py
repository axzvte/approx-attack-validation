#!/usr/bin/env python3
"""Batch U2-Net inference. Preserve old masks when exporting saliency scores."""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image

from infer_u2net import (
    infer,
    load_model,
    preprocess,
    resize_probability_map,
    select_device,
)


def parse_args():
    parser = argparse.ArgumentParser(description="Export PASCAL-S U2-Net saliency maps")
    parser.add_argument("--data", type=Path, default=Path.home() / "datasets/PASCAL-S")
    parser.add_argument("--start", type=int, default=1)
    parser.add_argument("--count", type=int, default=850)
    parser.add_argument("--device", choices=["auto", "cpu", "cuda"], default="auto")
    parser.add_argument("--repo", type=Path, default=Path("U-2-Net"))
    parser.add_argument("--saliency-only", action="store_true",
                        help="Save grayscale predictions without touching pred_masks")
    parser.add_argument("--skip-existing", action="store_true",
                        help="Resume without recalculating existing saliency files")
    parser.add_argument("--overwrite-masks", action="store_true",
                        help="Opt-in replacement of existing 0.5 binary masks")
    return parser.parse_args()


def main():
    args = parse_args()
    if args.start < 1 or args.count < 1 or args.start + args.count - 1 > 850:
        raise ValueError("Only PASCAL-S image IDs 1..850 are supported")

    root = args.data.expanduser().resolve()
    img_dir = root / "datasets/imgs/pascal"
    sal_dir = root / "pred_saliency"
    mask_dir = root / "pred_masks"
    repo = args.repo.expanduser().resolve()
    weights = repo / "saved_models/u2net/u2net.pth"
    sal_dir.mkdir(parents=True, exist_ok=True)
    if not args.saliency_only:
        mask_dir.mkdir(parents=True, exist_ok=True)

    device = select_device(args.device)
    print(f"Loading U2-Net on {device}; writing grayscale maps to {sal_dir}")
    model = load_model(repo, weights, device)

    for index, image_id in enumerate(range(args.start, args.start + args.count), 1):
        image_path = img_dir / f"{image_id}.jpg"
        sal_path = sal_dir / f"{image_id}_saliency.png"
        if not image_path.is_file():
            raise FileNotFoundError(image_path)

        if args.skip_existing and sal_path.is_file():
            print(f"[{index}/{args.count}] {image_id}: existing saliency, skipped")
            continue

        with Image.open(image_path) as src:
            image = src.convert("RGB")
        probability = infer(model, preprocess(image), device)
        saliency_u8 = resize_probability_map(probability, image.size)
        Image.fromarray(saliency_u8, mode="L").save(sal_path)

        if not args.saliency_only:
            mask_path = mask_dir / f"{image_id}_mask.png"
            if not mask_path.exists() or args.overwrite_masks:
                threshold_byte = round(0.50 * 255)
                binary = (saliency_u8 >= threshold_byte).astype(np.uint8) * 255
                Image.fromarray(binary, mode="L").save(mask_path)
            else:
                print(f"  Preserved existing binary mask: {mask_path.name}")
        print(f"[{index}/{args.count}] image {image_id}: {sal_path.name}")

    print(f"Finished exporting grayscale saliency maps: {sal_dir}")
    if args.saliency_only:
        print("Previous pred_masks/ files were not touched.")


if __name__ == "__main__":
    main()

