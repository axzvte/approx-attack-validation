"""Offline U2-Net pair contract (also shipped by axzvte/u2net_roi).

Pillow is imported only when validating pixels; legacy workflows do not need it.
No image or mask is resized, thresholded or inferred here.
"""
import argparse
import hashlib
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def resolve_roi_path(roi_dir, image_id):
    if isinstance(image_id, bool) or not isinstance(image_id, int) or not 1 <= image_id <= 40:
        raise ValueError('Image ID must be an integer in 1..40')
    result = Path(roi_dir) / f'image_{image_id:02d}_mask.png'
    if not result.is_file():
        raise FileNotFoundError(f'Missing per-image ROI (no shared-mask fallback): {result}')
    return result


def validate_pair(image_path, mask_path):
    from PIL import Image
    image_path, mask_path = Path(image_path), Path(mask_path)
    with Image.open(image_path) as image:
        image.load()
        size = image.size
    with Image.open(mask_path) as mask:
        if mask.format != 'PNG' or mask.mode not in ('1', 'L'):
            raise ValueError(f'ROI must be a single-channel 8-bit/1-bit PNG: {mask_path}')
        if mask.size != size:
            raise ValueError(f'Image/ROI size mismatch: image={size}, mask={mask.size}: {mask_path}')
        histogram = mask.convert('L').histogram()
        values = {i for i, count in enumerate(histogram) if count}
        if not (values <= {0, 1} or values <= {0, 255}):
            raise ValueError(f'ROI must be binary 0/1 or 0/255, got {sorted(values)}: {mask_path}')
        total = size[0] * size[1]
        roi_pixels = total - histogram[0]
        if not 0 < roi_pixels < total:
            raise ValueError(f'ROI and background must both be nonempty: {mask_path}')
    return dict(image=str(image_path.resolve()), mask=str(mask_path.resolve()), width=size[0], height=size[1],
                roi_pixels=roi_pixels, roi_fraction=roi_pixels / total,
                image_sha256=digest(image_path), mask_sha256=digest(mask_path))


def main(argv=None):
    parser = argparse.ArgumentParser(description='Validate staged image/U2-Net mask pairs without modifying data')
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--roi-dir', type=Path, help='Default: DATA/roi')
    parser.add_argument('--images', default='1:10', help='Inclusive IDs, e.g. 1:10 or 31:40')
    parser.add_argument('--report', type=Path, help='Optional NEW JSON report; existing files are never replaced')
    args = parser.parse_args(argv)
    first, last = map(int, args.images.split(':'))
    if not 1 <= first <= last <= 40:
        raise ValueError('Image range must stay in 1..40')
    rows = []
    for image_id in range(first, last + 1):
        stage = args.data / f'stage{(image_id-1)//10+1}'
        candidates = [stage / 'input' / f'image_{image_id:02d}.jpg', stage / f'image_{image_id:02d}.jpg']
        image_path = next((p for p in candidates if p.is_file()), None)
        if image_path is None:
            raise FileNotFoundError(f'Missing staged input: {candidates}')
        row = validate_pair(image_path,
                            resolve_roi_path(args.roi_dir or args.data / 'roi', image_id))
        rows.append(dict(image_id=image_id, **row))
    result = dict(roi_source='offline_u2net', runtime_hardware=False, pairs=rows)
    if args.report:
        if any(part.casefold() == 'sources' for part in args.report.resolve().parts):
            raise ValueError('Synced sources are read-only')
        with args.report.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2); stream.write('\n')
    print(f'Validated {len(rows)} per-image ROI pairs; no input data changed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
