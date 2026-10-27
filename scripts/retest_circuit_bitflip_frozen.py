#!/usr/bin/env python3
"""User-run new-image calibration with existing, training-only frozen hardware.

No training, hardware selection, seed updates or ablations. Each legal output
exports exactly two triptychs: whole image and the shared-mask ROI bounding box.
The author must not run this script, tests or experiment executables.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import re
import shutil
import sys
import time

import run_sharpen_bitflip_hardware as protocol
from run_sharpen_bitflip_batch import atomic_csv, atomic_json, process, read_csv
from ten_train_report import _quality, validate_training_report


CONTROL_RANGES = {'beam': (1, 64), 'refinement': (0, 10), 'restarts': (1, 8),
                  'boundary_budget': (0, 8192), 'context_budget': (1, 1000000),
                  'max_full_evals': (1, 10000000), 'full_candidates': (1, 1024)}
NATIVE_NAMES = {'max_full_evals': 'max_full_evaluations', 'global_psnr': 'global_psnr_threshold'}
FROZEN = ('frozen_hardware.csv', 'frozen_seeds.csv')
TRAIN_FILES = ('RUN.txt', 'training_baselines.csv', 'candidate_hardware.csv',
               'training_ranking.csv', 'training_evaluations.csv',
               'training_configurations.csv', 'search_stats.csv', *FROZEN)


def load_json(path):
    value = json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(value, dict):
        raise ValueError('Expected JSON object: ' + str(path))
    return value


def overlap(a, b):
    return a == b or a in b.parents or b in a.parents


def controls(options):
    result = {}
    for name, (lower, upper) in CONTROL_RANGES.items():
        value = options[name]
        if type(value) is not int or not lower <= value <= upper:
            raise ValueError('Invalid original calibration option: ' + name)
        result[name] = value
    if result['boundary_budget'] == 1:
        raise ValueError('Original boundary budget must be0 or>=2')
    for name, upper in (('global_psnr', 1000), ('convergence_tolerance', 1)):
        value = float(options[name])
        if not math.isfinite(value) or not 0 <= value <= upper:
            raise ValueError('Invalid original calibration option: ' + name)
        result[name] = value
    return result


def frozen_source(source, application, mask_hash):
    """Verify train evidence; never consult old test-image quality or pixels."""
    run = load_json(source / 'RUN.json')
    frozen = load_json(source / 'frozen_protocol.json')
    if (run.get('completed') is not True or run.get('all_succeeded') is not True or
            run.get('workflow') != 'ten_train' or run.get('selection_status') != 'selected' or
            frozen.get('schema') != 'ten_train_v1'):
        raise ValueError('Source must be a completed ten-image frozen-hardware run: ' + str(source))
    for metadata in (run, frozen):
        if (metadata.get('application') != application or
                metadata.get('splits', {}).get('train') != list(range(1, 11)) or
                metadata.get('roi_sha256') != mask_hash):
            raise ValueError('Source application, ten-image training split or original ROI disagrees')
    settings = controls(frozen['options'])
    if settings != controls(run['options']):
        raise ValueError('Frozen calibration controls disagree with the original run')
    native = protocol.APPLICATION_NODES[application]
    evidence = validate_training_report(source / 'train', application, list(range(1, 11)),
        settings['global_psnr'], int(run['options']['max_train_evaluations']), int(run['options']['max_candidates']))
    selected = evidence['selected']
    if selected is None:
        raise ValueError('Source has no complete numeric-eligible training hardware')
    hardware = protocol.hardware_key(read_csv(source / FROZEN[0], ['node_id', 'monitor', 'bit']), native)
    if (hardware != selected['key'] or frozen.get('hardware_key') != [list(n) for n in hardware] or
            frozen.get('hardware_id') != selected['hardware_id'] or
            run.get('selected_hardware_id') != selected['hardware_id']):
        raise ValueError('Frozen hardware disagrees with the training-only selection')
    for name in FROZEN:
        actual = protocol.digest(source / name)
        if (actual != frozen.get('frozen_hashes', {}).get(name) or
                actual != run.get('frozen_hashes', {}).get(name) or
                actual != protocol.digest(source / 'train' / name)):
            raise ValueError('Frozen hardware or train-only seed bank changed: ' + name)
    watched = [source / 'RUN.json', source / 'frozen_protocol.json', *(source / n for n in FROZEN),
               *(source / 'train' / n for n in TRAIN_FILES)]
    return dict(application=application, source=source, hardware=hardware,
                hardware_id=selected['hardware_id'], controls=settings,
                warm_seeds=len({r['seed_id'] for r in evidence['seed_rows']}),
                watched={p: protocol.digest(p) for p in watched})


def image_files(folder):
    def order(path):
        return [((0, int(part)) if part.isdigit() else (1, part.casefold()))
                for part in re.split(r'(\d+)', path.name)]
    paths = sorted((p for p in folder.iterdir() if p.is_file() and
                    p.suffix.casefold() in ('.jpg', '.jpeg', '.png', '.bmp', '.pgm')), key=order)
    if not paths:
        raise ValueError('No test images found directly in ' + str(folder))
    stems = [p.stem.casefold() for p in paths]
    if len(stems) != len(set(stems)):
        raise ValueError('Test images must have distinct filename stems')
    if len(paths) > 1000000:
        raise ValueError('Too many test images for diagnostic ids')
    return paths


def verify_unchanged(watched):
    for path, expected in watched.items():
        if protocol.digest(path) != expected:
            raise ValueError('Read-only input or frozen evidence changed: ' + str(path))


def validate_task(folder, image, context, source_image, mask, prefix):
    settings = context['controls']
    flags = protocol.flags_file(folder / 'RUN.txt')
    if (Path(flags.get('input_image', '')).resolve() != source_image.resolve() or
            Path(flags.get('roi', '')).resolve() != mask.resolve() or
            flags.get('image_mode') != 'comparisons' or flags.get('continuous') != '1' or
            flags.get('input_bits') != 'signed12' or flags.get('output_bits') != 'signed13_non_sign_0..11' or
            flags.get('baseline') != 'original_application_default' or
            flags.get('reference') != 'application_exact_output' or
            flags.get('warm_seeds') != str(context['warm_seeds'])):
        raise ValueError('Fixed evaluator used a different input, ROI, baseline, seed policy or search mode; rebuild it')
    for name, expected in settings.items():
        native_name = NATIVE_NAMES.get(name, name)
        if not math.isclose(float(flags[native_name]), expected, rel_tol=1e-12, abs_tol=1e-12):
            raise ValueError('Fixed evaluator changed original search controls: ' + name)
    tables, row = protocol.fixed_report(folder, image, context['hardware'], 'comparisons', prefix,
        settings['global_psnr'], protocol.APPLICATION_NODES[context['application']], context['application'])
    stats = tables['search_stats_all.csv'][0]
    scoring = 'full_joint_image_reconstruction' if context['application'] == 'dct' else 'exact_conditional_pixel_replay'
    if (stats.get('continuous') != '1' or stats.get('scoring') != scoring or
            int(stats['contexts_built']) > settings['context_budget'] or
            int(stats['full_evaluations']) > settings['max_full_evals']):
        raise ValueError('Fixed evaluator scoring or budgets disagree with the frozen protocol')
    baseline = next(r for r in tables['summary_all.csv'] if r['status'] == 'reference')
    _quality(baseline)
    if row['status'] != 'no_legal_interval_state':
        _quality(row)
    for region in ('global', 'roi', 'non_roi'):
        row['baseline_' + region + '_psnr'] = baseline[region + '_psnr']
    return tables, row


def main(argv=None):
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('data', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--test-dir', type=Path, help='Default: DATA/stage5; all directly contained images')
    parser.add_argument('--source', action='append', default=[], metavar='APP=FROZEN_REPORT',
                        help='Repeat for desired applications. Defaults to the four previous frozen runs.')
    parser.add_argument('--fixed-exe', type=Path, default=root / 'build-bitflip/circuit_bitflip_fixed')
    args = parser.parse_args(argv)
    begin = time.monotonic()
    owned = False
    meta = None
    statuses, best, image_rows, configurations, stats_rows = [], [], [], [], []
    contexts = []
    application_state = {}

    def flush():
        meta['total_wall_seconds'] = time.monotonic() - begin
        apps = []
        for context in contexts:
            app = context['application']
            outcomes = [r for r in best if r['application'] == app]
            measured = [r for r in outcomes if r['status'] in ('numeric_eligible', 'quality_not_met')]
            summary = dict(application=app, hardware_id=context['hardware_id'], installed_nodes=len(context['hardware']),
                status=application_state.get(app, 'pending'), hardware_selection_wall_seconds=0,
                test_images=len(paths), attempted_images=sum(r['application'] == app for r in statuses),
                metric_images=len(measured), numeric_test_successes=sum(r['status'] == 'numeric_eligible' for r in outcomes),
                test_wall_seconds=sum(r['elapsed_seconds'] for r in statuses if r['application'] == app))
            for region in ('global', 'roi', 'non_roi'):
                values = [float(r[region + '_psnr']) for r in measured]
                summary['mean_' + region + '_psnr'] = sum(values) / len(values) if values else ''
            apps.append(summary)
        atomic_csv(out / 'applications.csv', apps, ['application', 'status', 'hardware_selection_wall_seconds'])
        atomic_csv(out / 'best_per_image.csv', best, ['application', 'image', 'input_file', 'status',
            'global_psnr', 'roi_psnr', 'non_roi_psnr', 'baseline_global_psnr', 'baseline_roi_psnr',
            'baseline_non_roi_psnr', 'comparison_path', 'roi_comparison_path'])
        atomic_csv(out / 'run_status.csv', statuses, ['application', 'image', 'input_file', 'status',
            'returncode', 'elapsed_seconds', 'log', 'detail'])
        for name, records in (('images_all.csv', image_rows), ('configurations_all.csv', configurations),
                              ('search_stats_all.csv', stats_rows)):
            atomic_csv(out / name, records, ['application', 'image', 'input_file'])
        atomic_json(out / 'RUN.json', meta)

    try:
        data, out, fixed = args.data.resolve(), args.output.resolve(), args.fixed_exe.resolve()
        test_dir = (args.test_dir or data / 'stage5').resolve()
        if any(part.casefold() == 'sources' for part in out.parts):
            raise ValueError('Synced sources are read-only')
        if out.exists():
            raise ValueError('Output exists; choose a new directory')
        if not fixed.is_file():
            raise ValueError('Missing fixed-H evaluator; build circuit_bitflip_fixed first')
        supplied = {}
        if args.source:
            for entry in args.source:
                app, separator, path = entry.partition('=')
                if not separator or not path or app not in protocol.APPLICATION_NODES or app in supplied:
                    raise ValueError('Use one --source APP=FROZEN_REPORT per application')
                supplied[app] = Path(path).resolve()
        else:
            reports = root / 'results/bitflip'
            other = reports / 'other_apps_ten_train_scheduled_20261026'
            supplied = {'sharpen': other / 'sharpen', 'conv': other / 'conv',
                        'sobel': reports / 'sobel_ten_train_scheduled_20261026', 'dct': other / 'dct'}
        for reference in (data, test_dir, fixed.parent, *supplied.values()):
            if overlap(out, reference.resolve()):
                raise ValueError('Output must not overlap input data, earlier reports or the evaluator directory')
        paths = image_files(test_dir)
        # Resolve symlinks before any output creation so output cannot contain
        # an input target or overwrite it through a different directory name.
        if any(overlap(out, p.resolve()) for p in paths):
            raise ValueError('Output overlaps a test-image target')
        mask = data / 'mask/roi_mask.jpg'
        mask_hash = protocol.digest(mask)
        contexts = [frozen_source(source, app, mask_hash) for app, source in supplied.items()]
        inputs = {p: protocol.digest(p) for p in paths}
        immutable = {mask: mask_hash, fixed: protocol.digest(fixed)}
        for context in contexts:
            immutable.update(context['watched'])
        out.mkdir(parents=True, exist_ok=False)
        owned = True
        (out / 'logs').mkdir()
        for context in contexts:
            target = out / context['application']
            target.mkdir()
            for name in (*FROZEN, 'frozen_protocol.json'):
                shutil.copyfile(context['source'] / name, target / name)
                immutable[target / name] = protocol.digest(context['source'] / name)
        meta = dict(workflow='frozen_new_image_retest', completed=False, all_succeeded=False,
            training_evaluations=0, hardware_selection_wall_seconds=0, applications=list(supplied),
            test_directory=str(test_dir), test_images=[p.name for p in paths],
            input_sha256={p.name: value for p, value in inputs.items()}, roi_sha256=mask_hash,
            roi_policy='original shared mask, nearest-neighbor resized; not semantic subject detection',
            image_mode='comparisons', visual_status='pending_user_review', ablations='disabled',
            seed_policy='unchanged train-only frozen bank reset independently for each image',
            fixed_executable_sha256=immutable[fixed], total_wall_seconds=0, failed_tasks=[],
            sources={c['application']: dict(directory=str(c['source']), hardware_id=c['hardware_id'],
                hardware_key=c['hardware'], controls=c['controls']) for c in contexts})
        flush()
        for context in contexts:
            app = context['application']
            application_state[app] = 'running'
            for image, path in enumerate(paths, 1):
                prefix = f'{app}/test/{path.stem}'
                target = out / prefix
                target.parent.mkdir(parents=True, exist_ok=True)
                state = dict(application=app, image=image, input_file=path.name, status='running', returncode='',
                             elapsed_seconds=0, log=f'logs/{app}_{image:04d}.log', detail='')
                statuses.append(state)
                started = time.monotonic()
                try:
                    verify_unchanged({**immutable, path: inputs[path]})
                    command = [fixed, app, data, target, '--hardware', out / app / FROZEN[0],
                               '--seeds', out / app / FROZEN[1], '--input-image', path,
                               '--image', image, '--image-mode', 'comparisons', '--continuous']
                    for name, value in context['controls'].items():
                        command.extend(['--' + name.replace('_', '-'), value])
                    state['returncode'] = process(command, out / state['log'], root)
                    if state['returncode']:
                        raise RuntimeError('Fixed evaluator returned ' + str(state['returncode']))
                    verify_unchanged({**immutable, path: inputs[path]})
                    tables, row = validate_task(target, image, context, path, mask, prefix)
                    row.update(application=app, input_file=path.name, hardware_id=context['hardware_id'])
                    best.append(row)
                    for name, records in (('images_all.csv', image_rows),
                                          ('configurations_all.csv', configurations),
                                          ('search_stats_all.csv', stats_rows)):
                        records.extend(dict(r, application=app, image=image, input_file=path.name,
                                            hardware_id=context['hardware_id']) for r in tables[name])
                    state['status'] = 'completed'
                except KeyboardInterrupt:
                    state.update(status='interrupted', detail='Interrupted by user')
                    application_state[app] = 'interrupted'
                    raise
                except (OSError, ValueError, RuntimeError, KeyError, TypeError, csv.Error) as exc:
                    state.update(status='failed', detail=str(exc))
                    best.append(dict(application=app, image=image, input_file=path.name,
                                     hardware_id=context['hardware_id'], status='run_failed', visual_status='not_available'))
                    meta['failed_tasks'].append(dict(application=app, input_file=path.name, detail=str(exc)))
                    print(f'{app} {path.name} failed: {exc}', file=sys.stderr)
                finally:
                    state['elapsed_seconds'] = time.monotonic() - started
                    flush()
            application_state[app] = 'failed' if any(r['application'] == app and r['status'] == 'failed'
                                                     for r in statuses) else 'completed'
            flush()
        verify_unchanged({**immutable, **inputs})
        meta.update(completed=True, all_succeeded=not meta['failed_tasks'], frozen_evidence_unchanged=True)
        flush()
        print(f"Finished {len(contexts)} applications x {len(paths)} new images; visual review pending: {out}")
        return 0 if meta['all_succeeded'] else 1
    except KeyboardInterrupt:
        if owned and meta is not None:
            meta['error'] = 'Interrupted by user'
            flush()
        print('Interrupted; completed images and reports preserved', file=sys.stderr)
        return 130
    except (OSError, ValueError, RuntimeError, KeyError, TypeError, csv.Error) as exc:
        if owned and meta is not None:
            meta['error'] = str(exc)
            flush()
        print('Frozen new-image test failed: ' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
