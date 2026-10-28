#!/usr/bin/env python3
"""Add absolute PSNR labels to existing flat stage6 comparison PNGs.

Only reads completed reports and existing PNGs. No model, native executable,
training or interval search is imported or executed. Output must be a NEW folder.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

APPLICATIONS=('sharpen','conv','sobel','dct')
HEADER_HEIGHT=92

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def overlap(a,b):
    return a==b or a in b.parents or b in a.parents

def psnr(value):
    value=float(value)
    if math.isinf(value) and value>0:
        return 'inf'
    if not math.isfinite(value):
        raise ValueError('PSNR must be finite or positive infinity')
    return f'{value:.3f}'

def label_lines(row):
    return [
        ['exact','Global PSNR: inf dB','ROI PSNR: inf dB','Reference: exact'],
        ['baseline',f"Global PSNR: {psnr(row['baseline_global_psnr'])} dB",
         f"ROI PSNR: {psnr(row['baseline_roi_psnr'])} dB",'ROI: original mask'],
        ['fixed H + intervals',f"Global PSNR: {psnr(row['attack_global_psnr'])} dB",
         f"ROI PSNR: {psnr(row['attack_roi_psnr'])} dB",row['status']],
    ]

def preflight(source,output):
    source,output=Path(source).resolve(),Path(output).resolve()
    if output.exists():
        raise ValueError('Output exists; choose a NEW folder')
    if overlap(source,output) or any(p.casefold()=='sources' for p in output.parts):
        raise ValueError('Output must not overlap the source; synced sources are read-only')
    metadata=json.loads((source/'RUN.json').read_text(encoding='utf-8-sig'))
    if (metadata.get('completed') is not True or metadata.get('all_succeeded') is not True or
        metadata.get('workflow')!='stage6_offline_u2net_frozen_hardware_retest'):
        raise ValueError('Expected a completed flat stage6 fixed-hardware report')
    with (source/'per_image_metrics.csv').open(encoding='utf-8-sig',newline='') as f:
        rows=list(csv.DictReader(f))
    if not rows or len(rows)!=metadata.get('completed_tasks'):
        raise ValueError('Missing or incomplete per-image metrics')
    dimensions={int(p['image']):(int(p['width']),int(p['height'])) for p in metadata['input_images']}
    watched={source/'RUN.json':digest(source/'RUN.json'),source/'per_image_metrics.csv':digest(source/'per_image_metrics.csv')}
    seen=set();jobs=[];skipped=[]
    for row in rows:
        app=row['application'];index=int(row['image'])
        if app not in APPLICATIONS or index<=0 or (app,index) in seen:
            raise ValueError('Unknown or duplicate application/image identity')
        seen.add((app,index))
        if row['status']=='no_legal_interval_state':
            if row.get('comparison_path') or row.get('roi_comparison_path'):
                raise ValueError('A no-state result must not claim comparison images')
            skipped.append(dict(application=app,image=index,status=row['status']))
            continue
        if row['status'] not in ('numeric_eligible','quality_not_met'):
            raise ValueError('Cannot annotate an unknown or failed task')
        labels=label_lines(row)
        for key,kind,old_header in [('comparison','overall',24),('roi_comparison','roi',48)]:
            relative=Path(row[key+'_path'])
            path=(source/relative).resolve()
            if relative.is_absolute() or source not in path.parents:
                raise ValueError('Comparison path escapes source directory')
            sha=digest(path)
            if sha!=row[key+'_sha256']:
                raise ValueError('Source comparison SHA-256 differs from report: '+str(path))
            watched[path]=sha
            with Image.open(path) as image:
                if image.mode!='L' or image.width%3 or image.height<=old_header:
                    raise ValueError('Expected a grayscale legacy triptych')
                if kind=='overall' and image.size!=(3*dimensions[index][0],dimensions[index][1]+24):
                    raise ValueError('Unsupported/already-labeled comparison layout; original 24px header required')
                if kind=='roi' and (image.width%6 or (image.height-48)%2):
                    raise ValueError('Expected a legacy 2x nearest-neighbor ROI triptych')
                image.verify()
            jobs.append(dict(source=path,output=f'{app}_{index:02d}_{kind}.png',old_header=old_header,
                             labels=labels,application=app,image=index,status=row['status']))
    return source,output,jobs,watched,skipped

def draw_comparison(path,old_header,labels):
    for name in ('DejaVuSans.ttf','Arial.ttf'):
        try:
            font=ImageFont.truetype(name,15)
            break
        except OSError:
            font=None
    if font is None:
        try:
            font=ImageFont.load_default(size=15)
        except TypeError:  # Pillow 10.0 compatibility.
            font=ImageFont.load_default()
    with Image.open(path) as original:
        original.load()
        content_width=original.width//3
        content_height=original.height-old_header
        frames=[original.crop((i*content_width,old_header,(i+1)*content_width,original.height)) for i in range(3)]
    width=max(260,content_width,max(math.ceil(font.getlength(t))+16 for col in labels for t in col))
    canvas=Image.new('L',(width*3,HEADER_HEIGHT+content_height),0)
    draw=ImageDraw.Draw(canvas)
    left=(width-content_width)//2
    for col,frame in enumerate(frames):
        canvas.paste(frame,(col*width+left,HEADER_HEIGHT))
        for row,line in enumerate(labels[col]):
            draw.text((col*width+8,5+21*row),line,font=font,fill=255)
    return canvas,dict(header_height=HEADER_HEIGHT,panel_width=width,content_left=left,content_width=content_width)

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--check',action='store_true',help='Validate reports/images without creating output')
    args=parser.parse_args(argv)
    try:
        source,output,jobs,watched,skipped=preflight(args.source,args.output)
        if args.check:
            print(f'Validated {len(jobs)} comparison images; no output created')
            return 0
        output.mkdir(parents=True,exist_ok=False)
        records=[]
        for job in jobs:
            if digest(job['source'])!=watched[job['source']]:
                raise ValueError('Source image changed during annotation')
            image,layout=draw_comparison(job['source'],job['old_header'],job['labels'])
            image.save(output/job['output'])
            records.append(dict(**{k:v for k,v in job.items() if k!='source'},**layout,
                source=str(job['source']),source_sha256=watched[job['source']],output_sha256=digest(output/job['output'])))
        if any(digest(path)!=sha for path,sha in watched.items()):
            raise ValueError('Source report or image changed during annotation')
        manifest=dict(completed=True,source=str(source),operation='annotation_only',
            psnr_reference='exact application output; peak=255; ROI uses original binary mask, not crop rectangle',
            psnr_units='dB',source_reports_unchanged=True,skipped=skipped,images=records)
        (output/'annotation_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(f'Saved {len(records)} annotated comparisons in one flat folder: {output}')
        return 0
    except (OSError,ValueError,KeyError,TypeError,csv.Error) as exc:
        print('Cannot annotate: '+str(exc),file=sys.stderr)
        return 1

if __name__=='__main__':
    raise SystemExit(main())
