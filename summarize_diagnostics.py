"""Summarize recorded renderer diagnostics; never contacts or changes the TSP."""
import collections
import json
import pathlib
import re
import statistics

root = pathlib.Path(__file__).resolve().parent / 'build/glprof-results'

def fields(line):
    result = {}
    for key, value in re.findall(r'(\w+)=([^ ]+)', line):
        try:
            result[key] = float(value) if '.' in value else int(value)
        except ValueError:
            result[key] = value
    return result

report = {}
for path in sorted(root.glob('*.log')):
    if not path.name.startswith(('vtx-', 'phases-', 'gpu-')):
        continue
    lines = path.read_text(errors='replace').splitlines()
    summary = {}
    phase_windows = []
    for line in lines:
        if line.startswith('phases:'):
            phase_windows.append({'total': fields(line), 'phases': {}})
        elif line.startswith('phase:') and phase_windows:
            phase_windows[-1]['phases'][line.split()[1]] = fields(line)
    if phase_windows:
        summary['last_three_windows'] = phase_windows[-3:]
        for w in phase_windows:
            if w['total'].get('gl_ms') and w['total']['runs']:
                accounted = sum(p['ms'] for p in w['phases'].values()) / w['total']['runs']
                residual = w['total']['run_ms'] - accounted - w['total']['gl_ms']
                w['total']['unaccounted_ms'] = residual
                # Per-pass log serialization occurs at the Run scope teardown,
                # outside the nested phase scopes in the GPU diagnostic variant.
                if not path.name.startswith('gpu-'):
                    assert abs(residual) < .15, (path, w)
    frames = [fields(x) for x in lines if x.startswith('vtxframe:')][-100:]
    if frames:
        summary['last_100_vertex_frames_mean'] = {
            k: statistics.mean(f[k] for f in frames)
            for k in ('vertices', 'vertex_bytes', 'bpv', 'index_bytes', 'merge_index_bytes',
                      'identical_address_pct', 'identical_logical_pct', 'identical_content_pct')
        }
        summary['vertex_frames'] = len(frames)
        for count in (100, 20):
            selected = frames[-count:]
            size = sum(f['vertex_bytes'] for f in selected)
            summary[f'last_{count}_weighted_identical_pct'] = {
                k: sum(f[k] * f['vertex_bytes'] for f in selected) / size if size else 0
                for k in ('identical_address_pct', 'identical_logical_pct', 'identical_content_pct')
            }
    aux = [fields(x) for x in lines if x.startswith('finish-aux:')]
    if aux:
        # Last 200 rendered chunks, not startup/title quads.
        sequences = sorted({x['seq'] for x in aux})[-200:]
        aux = [x for x in aux if x['seq'] in sequences]
        groups = collections.defaultdict(list)
        for x in aux:
            groups[(x['kind'], str(x['target']))].append(x)
        summary['finish_aux'] = {
            f'{key[0]} {key[1]}': {
                'calls': len(values),
                **{k: statistics.mean(v[k] for v in values)
                   for k in ('pre_ms', 'draw_ms', 'post_ms', 'isolated_ms')},
            } for key, values in groups.items()
        }
        summary['finish_aux_mean_per_chunk'] = {
            k: sum(v[k] for v in aux) / len(sequences)
            for k in ('pre_ms', 'draw_ms', 'post_ms', 'isolated_ms')
        }
    summary['extensions'] = [x for x in lines if x.startswith(('glext:', 'gputime-init:'))]
    summary['calibration'] = [x for x in lines if x.startswith('profcal:')]
    report[path.stem] = summary

output = root / 'diagnostics-summary.json'
output.write_text(json.dumps(report, indent=2) + '\n')
print(output)
for name, entry in report.items():
    print(name)
    if 'last_three_windows' in entry:
        print('  Run ms:', [w['total']['run_ms'] for w in entry['last_three_windows']])
    if 'last_100_vertex_frames_mean' in entry:
        print('  Vertices:', entry['last_100_vertex_frames_mean'])
    if 'finish_aux_mean_per_chunk' in entry:
        print('  Finish aux/chunk:', entry['finish_aux_mean_per_chunk'])
