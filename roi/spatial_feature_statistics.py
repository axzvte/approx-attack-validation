#!/usr/bin/env python3
"""Describe existing binary masks and repeated split-half spatial frequencies.

Only NumPy and Pillow are required. See SPATIAL_FEATURE_STATISTICS.md for the
sampling convention, padding denominators and limits of these descriptive data.
"""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw, __version__ as pillow_version


THRESHOLDS = (.25, .50, .75, .90)


def read_mask(path):
    with Image.open(path) as image:
        if image.format != 'PNG' or image.mode not in ('1', 'L'):
            raise ValueError(f'Expected a single-channel binary PNG: {path}')
        array = np.array(image)
    values = set(np.unique(array).tolist())
    if not (values <= {0, 1} or values <= {0, 255}):
        raise ValueError(f'Expected values 0/1 or 0/255, found {sorted(values)}: {path}')
    return array > 0


def sample_mask(mask, grid, mode):
    """Sample continuous source pixels at canvas pixel centers; no smoothing."""
    if grid < 1 or mode not in ('letterbox', 'stretch'):
        raise ValueError('Invalid grid or coordinate mode')
    height, width = mask.shape
    if mode == 'letterbox':
        sx = sy = min(grid / width, grid / height)
    else:
        sx, sy = grid / width, grid / height
    dx, dy = (grid - width * sx) / 2, (grid - height * sy) / 2
    x = (np.arange(grid) + .5 - dx) / sx
    y = (np.arange(grid) + .5 - dy) / sy
    valid = ((y >= 0) & (y < height))[:, None] & ((x >= 0) & (x < width))[None, :]
    xi = np.clip(np.floor(x).astype(int), 0, width - 1)
    yi = np.clip(np.floor(y).astype(int), 0, height - 1)
    sampled = mask[np.ix_(yi, xi)] & valid
    return sampled, valid


def frequency_map(masks, valid_maps):
    hits = np.sum(masks, axis=0, dtype=np.int32)
    counts = np.sum(valid_maps, axis=0, dtype=np.int32)
    frequency = np.full(counts.shape, np.nan, dtype=np.float64)
    np.divide(hits, counts, out=frequency, where=counts > 0)
    return frequency, counts


def half_splits(count, repeats, seed):
    if count < 2 or repeats < 1:
        raise ValueError('At least two images and one repeat are required')
    rng = np.random.default_rng(seed)
    result = []
    for _ in range(repeats):
        order = rng.permutation(count).tolist()
        result.append((order[:count // 2], order[count // 2:]))
    return result


def compare_maps(first, second):
    common = np.isfinite(first) & np.isfinite(second)
    a, b = first[common], second[common]
    result = dict(common_cells=int(common.sum()), mae=None, rmse=None, pearson_r=None)
    if a.size:
        result['mae'] = float(np.mean(np.abs(a - b)))
        result['rmse'] = float(np.sqrt(np.mean((a - b) ** 2)))
        if np.std(a) > 1e-12 and np.std(b) > 1e-12:
            result['pearson_r'] = float(np.corrcoef(a, b)[0, 1])
    for threshold in THRESHOLDS:
        selected_a, selected_b = a >= threshold, b >= threshold
        union = int(np.count_nonzero(selected_a | selected_b))
        overlap = int(np.count_nonzero(selected_a & selected_b))
        result[f'iou_ge_{threshold:.2f}'] = overlap / union if union else None
        result[f'a_cells_ge_{threshold:.2f}'] = int(selected_a.sum())
        result[f'b_cells_ge_{threshold:.2f}'] = int(selected_b.sum())
    return result


def describe(values):
    array = np.asarray([v for v in values if v is not None], dtype=float)
    if not array.size:
        return dict(n=0, mean=None, median=None, p10=None, p90=None, min=None, max=None)
    return dict(n=int(array.size), mean=float(array.mean()), median=float(np.median(array)),
                p10=float(np.quantile(array, .1)), p90=float(np.quantile(array, .9)),
                min=float(array.min()), max=float(array.max()))


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def write_csv(path, rows):
    with path.open('w', encoding='utf-8-sig', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def heatmap(values, title, size=420):
    """Fixed 0..1 sequential scale for every frequency and difference panel."""
    scaled = np.clip(np.nan_to_num(values, nan=0), 0, 1)
    low, high = np.array([247, 251, 255]), np.array([8, 48, 107])
    rgb = np.round(low + scaled[..., None] * (high - low)).astype(np.uint8)
    rgb[~np.isfinite(values)] = [175, 175, 175]
    panel = Image.new('RGB', (size + 60, size + 110), 'white')
    draw = ImageDraw.Draw(panel)
    draw.text((20, 12), title, fill='black')
    panel.paste(Image.fromarray(rgb).resize((size, size), Image.Resampling.NEAREST), (30, 42))
    draw.rectangle((30, 42, size + 29, size + 41), outline='black')
    draw.text((30, size + 48), 'Top-left origin; x right, y down', fill='black')
    draw.text((30, size + 65), 'Gray = no valid image samples', fill='black')
    for x in range(size):
        color = tuple(np.round(low + x / (size - 1) * (high - low)).astype(int))
        draw.line((30 + x, size + 85, 30 + x, size + 94), fill=color)
    draw.text((30, size + 96), '0', fill='black')
    draw.text((size + 15, size + 96), '1', fill='black')
    return panel


def area_plot(values, path):
    hist, _ = np.histogram(values, bins=np.linspace(0, 1, 21))
    image = Image.new('RGB', (1000, 510), 'white')
    draw = ImageDraw.Draw(image)
    draw.text((65, 18), 'Original-resolution mask area / image area', fill='black')
    draw.text((65, 40), f'N = {len(values)}; mean = {np.mean(values):.4f}; median = {np.median(values):.4f}', fill='black')
    maximum = max(1, int(hist.max()))
    for tick in range(6):
        y = 430 - tick * 70
        draw.line((65, y, 965, y), fill='#dddddd')
        draw.text((8, y - 5), f'{maximum * tick / 5:.1f}', fill='black')
    for index, count in enumerate(hist):
        x = 65 + index * 45
        y = 430 - int(count / maximum * 350)
        draw.rectangle((x + 2, y, x + 42, 430), fill='#2166ac')
    for tick in range(11):
        draw.text((60 + tick * 90, 440), f'{tick / 10:.1f}', fill='black')
    draw.text((65, 475), 'Y = image count; bins width = 0.05; empty/full masks included', fill='black')
    image.save(path)


def analyze(args):
    data = args.data.expanduser().resolve()
    mask_dir = (data / args.mask_dir).resolve()
    image_dir = (data / args.image_dir).resolve()
    output = args.output.expanduser().resolve()
    if args.count < 2 or args.start < 1 or args.grid < 2 or args.repeats < 1 or args.seed < 0:
        raise ValueError('Require count >= 2, start >= 1, grid >= 2, repeats >= 1, seed >= 0')
    for source in (mask_dir, image_dir):
        if output.is_relative_to(source) or source.is_relative_to(output):
            raise ValueError('Output directory must be separate from input directories')
    if output.exists():
        raise ValueError(f'Output must be a new directory: {output}')
    pairs = [(i, image_dir / f'{i}.jpg', mask_dir / f'{i}_mask.png')
             for i in range(args.start, args.start + args.count)]
    for _, image_path, mask_path in pairs:
        for path in (image_path, mask_path):
            if not path.is_file():
                raise ValueError(f'Missing input: {path}')
    output.mkdir(parents=True, exist_ok=False)
    protocol = dict(status='running', started_utc=datetime.now(timezone.utc).isoformat(),
                    data=str(data), mask_dir=str(mask_dir), image_dir=str(image_dir),
                    count=args.count, start=args.start, grid=args.grid,
                    coordinate_mode=args.coordinate_mode, repeats=args.repeats, seed=args.seed,
                    thresholds=list(THRESHOLDS), numpy=np.__version__, pillow=pillow_version,
                    python=sys.version, script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    sampling='inverse affine at canvas pixel centers; half-open source pixels',
                    frequency='foreground image count / valid-content image count at each canvas center',
                    interpretation='descriptive split-half stability; no independent generalization claim')
    write_json(output / 'protocol.json', protocol)
    try:
        rows, masks, valid_maps = [], [], []
        for image_id, image_path, mask_path in pairs:
            mask = read_mask(mask_path)
            with Image.open(image_path) as image:
                if image.size != (mask.shape[1], mask.shape[0]):
                    raise ValueError(f'Image/mask size mismatch for ID {image_id}')
            mapped, valid = sample_mask(mask, args.grid, args.coordinate_mode)
            rows.append(dict(image_id=image_id, width=mask.shape[1], height=mask.shape[0],
                             foreground_pixels=int(mask.sum()), source_area_fraction=float(mask.mean()),
                             sampled_foreground_cells=int(mapped.sum()), valid_canvas_cells=int(valid.sum()),
                             vanished_after_sampling=bool(mask.any() and not mapped.any()),
                             image_path=str(image_path), mask_path=str(mask_path),
                             image_sha256=hashlib.sha256(image_path.read_bytes()).hexdigest(),
                             mask_sha256=hashlib.sha256(mask_path.read_bytes()).hexdigest()))
            masks.append(mapped)
            valid_maps.append(valid)
            if len(rows) % 50 == 0 or len(rows) == len(pairs):
                print(f'Read {len(rows)}/{len(pairs)} image-mask pairs', flush=True)
        write_csv(output / 'per_image.csv', rows)
        frequency, counts = frequency_map(masks, valid_maps)
        np.save(output / 'all_frequency.npy', frequency)
        np.save(output / 'valid_image_count.npy', counts)
        heatmap(frequency, f'All {len(rows)} images - conditional frequency').save(output / 'all_frequency.png')
        heatmap(counts / len(rows), 'Valid image fraction (content support)').save(output / 'valid_image_fraction.png')
        areas = [row['source_area_fraction'] for row in rows]
        area_plot(areas, output / 'area_distribution.png')
        split_dir = output / 'splits'
        split_dir.mkdir()
        metrics, memberships = [], []
        for repeat, (left, right) in enumerate(half_splits(len(rows), args.repeats, args.seed), 1):
            group_maps = []
            for group, indices in (('A', left), ('B', right)):
                current, support = frequency_map([masks[i] for i in indices], [valid_maps[i] for i in indices])
                group_maps.append(current)
                prefix = split_dir / f'repeat_{repeat:02d}_{group}'
                np.save(str(prefix) + '_frequency.npy', current)
                np.save(str(prefix) + '_valid_count.npy', support)
                memberships.extend(dict(repeat=repeat, group=group, image_id=rows[i]['image_id']) for i in indices)
            first, second = group_maps
            metrics.append(dict(repeat=repeat, images_a=len(left), images_b=len(right),
                                **compare_maps(first, second)))
            panels = [heatmap(first, f'Repeat {repeat}: A (n={len(left)})'),
                      heatmap(second, f'Repeat {repeat}: B (n={len(right)})'),
                      heatmap(np.abs(first - second), 'Absolute difference (same 0..1 scale)')]
            canvas = Image.new('RGB', (sum(p.width for p in panels), panels[0].height), 'white')
            for index, panel in enumerate(panels):
                canvas.paste(panel, (index * panel.width, 0))
            canvas.save(split_dir / f'repeat_{repeat:02d}_comparison.png')
            print(f'Split {repeat}/{args.repeats} complete', flush=True)
        write_csv(output / 'split_membership.csv', memberships)
        write_csv(output / 'split_stability.csv', metrics)
        summary = dict(images=len(rows), empty_masks=sum(a == 0 for a in areas),
                       full_masks=sum(a == 1 for a in areas),
                       vanished_after_sampling=sum(row['vanished_after_sampling'] for row in rows),
                       source_area_fraction=describe(areas), supported_canvas_cells=int((counts > 0).sum()),
                       min_positive_support=int(counts[counts > 0].min()) if (counts > 0).any() else None,
                       split_metrics={key: describe([row[key] for row in metrics])
                                      for key in ('mae', 'rmse', 'pearson_r', *[f'iou_ge_{t:.2f}' for t in THRESHOLDS])})
        write_json(output / 'summary.json', summary)
        report = ['# 图像特征空间描述统计', '',
                  f"图片数量 {len(rows)}，空掩膜 {summary['empty_masks']}，全图掩膜 {summary['full_masks']}。", '',
                  f"原尺寸面积占比均值 {np.mean(areas):.4f}，中位数 {np.median(areas):.4f}。", '',
                  '先查看 area_distribution.png、all_frequency.png、valid_image_fraction.png 和 splits/ 对比图。', '',
                  '频率按每个位置的有效图像数计算，留白不计作背景。不同位置的分母可能不同。', '',
                  f"降采样后消失的非空掩膜数量 {summary['vanished_after_sampling']}。面积统计始终来自原尺寸。", '',
                  '分组 MAE / RMSE 越小表示频率越接近，Pearson r 衡量空间变化的一致性。',
                  '固定频率阈值下的 IoU 衡量高频位置重合程度。常量图相关性和双空集合 IoU 记为空值。', '',
                  '重复划分共用同一批图片，指标分布属于描述性稳定性分析。高相关也可能出现在平坦或宽泛分布中。',
                  '这些输出需要结合频率图和逐图面积解读，尚未确定固定矩形或证明其覆盖效果。', '']
        (output / 'summary.md').write_text('\n'.join(report), encoding='utf-8')
        protocol['status'] = 'completed'
        protocol['completed_utc'] = datetime.now(timezone.utc).isoformat()
        write_json(output / 'protocol.json', protocol)
    except Exception as error:
        protocol.update(status='failed', error=str(error))
        write_json(output / 'protocol.json', protocol)
        raise
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=Path.home() / 'datasets/PASCAL-S')
    parser.add_argument('--mask-dir', default='pred_masks', help='Absolute or relative to --data')
    parser.add_argument('--image-dir', default='datasets/imgs/pascal', help='Absolute or relative to --data')
    parser.add_argument('--start', type=int, default=1)
    parser.add_argument('--count', type=int, default=850)
    parser.add_argument('--grid', type=int, default=320)
    parser.add_argument('--coordinate-mode', choices=('letterbox', 'stretch'), default='letterbox')
    parser.add_argument('--repeats', type=int, default=10)
    parser.add_argument('--seed', type=int, default=2026)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        output = analyze(args)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Error: {error}\n')
    print(f'Completed. Read {output / "summary.md"}')


if __name__ == '__main__':
    main()
