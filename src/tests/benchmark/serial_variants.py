#!/usr/bin/env python3
"""Measure archived builds serially in forward/reverse order, including warmup."""
import argparse
import json
import hashlib
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('assets', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('builds', nargs='+', help='NAME=ARCHIVED_BUILD')
parser.add_argument('--ir-mode', action='append', default=[], metavar='NAME=0/4/5/6/7/17',
                    help='Select compiler policy within an archived binary')
parser.add_argument('--tlb-hash', action='append', default=[], metavar='NAME=0/1')
parser.add_argument('--code-compare', action='append', default=[], metavar='NAME=0/2')
parser.add_argument('--unsafe-code',action='append',default=[],metavar='NAME=0/3')
parser.add_argument('--leaf-features',action='append',default=[],metavar='NAME=0/128')
parser.add_argument('--predicated-leaves', action='append', default=[], metavar='NAME=0/1')
parser.add_argument('--execution-limits', action='append', default=[], metavar='NAME=WINDOW,LEAF,SITES,RUNNER')
parser.add_argument('--input', action='append', default=[], metavar='NAME=INPUT', help='Optional per-variant guest input route')
parser.add_argument('--start-us', type=int, default=78000000)
parser.add_argument('--end-us', type=int, default=96000000)
args = parser.parse_args()
if not 0 <= args.start_us < args.end_us <= 120000000:
    parser.error('Window must be ordered and within 120 guest seconds')
variants = []
for item in args.builds:
    name, path = item.split('=', 1)
    if not name.replace('-', '').isalnum() or any(name == n for n, _ in variants):
        parser.error('Unique alphanumeric/hyphen names are required')
    variants.append((name, Path(path).resolve()))
modes = {}
for item in args.ir_mode:
    name, separator, value = item.partition('=')
    if not separator or name not in dict(variants) or name in modes or value not in ('0', '4', '5', '6', '7', '17'):
        parser.error('IR mode requires a unique known NAME=0/4/5/6/7/17')
    modes[name] = int(value)
compare_modes = {}
for item in args.code_compare:
    name, separator, value = item.partition('=')
    if not separator or name not in dict(variants) or name in compare_modes or value not in ('0', '2'):
        parser.error('Code compare requires a unique known NAME=0/2')
    compare_modes[name] = int(value)
hash_modes = {}
for item in args.tlb_hash:
    name, separator, value = item.partition('=')
    if not separator or name not in dict(variants) or name in hash_modes or value not in ('0', '1'):
        parser.error('TLB hash requires a unique known NAME=0/1')
    hash_modes[name] = int(value)
unsafe_modes = {}
for item in args.unsafe_code:
    name,separator,value=item.partition('=')
    if not separator or name not in dict(variants) or name in unsafe_modes or value not in ('0','3'):parser.error('Unsafe mode requires unique known NAME=0/3')
    unsafe_modes[name]=int(value)
features = {}
for item in args.leaf_features:
    name,separator,value=item.partition('=')
    if not separator or name not in dict(variants) or name in features or value not in ('0','128'):
        parser.error('Leaf features require unique known NAME=0/128')
    features[name]=int(value)
predicates = {}
for item in args.predicated_leaves:
    name, separator, value = item.partition('=')
    if not separator or name not in dict(variants) or name in predicates or value not in ('0','1'):
        parser.error('Leaf predication requires unique known NAME=0/1')
    predicates[name]=int(value)
limits = {}
for item in args.execution_limits:
    name, separator, value = item.partition('=')
    try:
        parts = [int(x) for x in value.split(',')]
        w,l,s,r = parts
        valid = value == ','.join(map(str,parts)) and 128<=w<=2048 and w%4==0 and 1<=l<=64 and 0<=s<=16 and 0<=r<=4096
    except ValueError:
        valid = False
    if not separator or name not in dict(variants) or name in limits or not valid:
        parser.error('Execution limits require unique known NAME=WINDOW,LEAF,SITES,RUNNER within supported bounds')
    limits[name] = parts
inputs = {}
for item in args.input:
    name, separator, value = item.partition('=')
    if not separator or name not in dict(variants) or name in inputs or not Path(value).is_file():
        parser.error('Input requires a unique known NAME=EXISTING_FILE')
    inputs[name] = Path(value).resolve()
args.output.mkdir()
root = Path(__file__).resolve().parents[3]
rows = []
for repetition, order in ((1, variants), (2, list(reversed(variants)))):
    for name, build in order:
        label = f'{name}-{repetition}'
        env = dict(os.environ, EKA2L1_WASM_BUILD_DIR=str(build),
                   EKA2L1_BENCHMARK_AOT='5', EKA2L1_GPU='hardware',
                   EKA2L1_PROFILE_DETAIL='0', EKA2L1_CHROME_TRACE='off',
                   EKA2L1_PROFILE_START_US=str(args.start_us))
        for key in ('PROFILE_GATE', 'EKA2L1_AOT_VERIFY', 'EKA2L1_GUEST_PROFILE',
                    'EKA2L1_AOT_DIAGNOSTICS', 'EKA2L1_EXIT_CENSUS', 'EKA2L1_V8_FLAGS', 'EKA2L1_V8_DUMP'):
            env.pop(key, None)
        env.pop('EKA2L1_PROFILE_INPUT', None)
        if name in inputs:
            env['EKA2L1_PROFILE_INPUT'] = str(inputs[name])
        env.pop('EKA2L1_TLB_HASH', None)
        if name in hash_modes:
            env['EKA2L1_TLB_HASH'] = str(hash_modes[name])
        env.pop('EKA2L1_CODE_COMPARE', None)
        if name in compare_modes:
            env['EKA2L1_CODE_COMPARE'] = str(compare_modes[name])
        env.pop('EKA2L1_AOT_IR_MODE', None)
        if name in modes:
            env['EKA2L1_AOT_IR_MODE'] = str(modes[name])
        env['EKA2L1_UNSAFE_CODE']=str(unsafe_modes.get(name,3))
        env['EKA2L1_LEAF_FEATURES']=str(features.get(name,0))
        env['EKA2L1_PREDICATED_LEAVES'] = str(predicates.get(name,0))
        env['EKA2L1_EXECUTION_LIMITS'] = ','.join(map(str,limits.get(name,[512,16,8,512])))
        output = args.output / label
        with (args.output / (label + '.log')).open('w') as log:
            subprocess.run(['node', 'profile.ts', str(args.assets.resolve()),
                            str(output.resolve()), '2', '0', str(args.end_us)],
                           cwd=root / 'src/tests/wasm', env=env, stdout=log,
                           stderr=subprocess.STDOUT, timeout=1800, check=True)
        report = json.loads((output / 'report.json').read_text())
        if report.get('unsafe_code') != unsafe_modes.get(name,3):raise RuntimeError('Wrong unsafe code mode')
        if report.get('leaf_features') != features.get(name,0):raise RuntimeError('Wrong leaf feature mask')
        if report.get('predicated_leaves') != predicates.get(name,0):
            raise RuntimeError('Wrong leaf predication mode')
        if report.get('execution_limits') != limits.get(name,[512,16,8,512]) or report.get('exit_census'):
            raise RuntimeError('Wrong execution limits or diagnostic census active in timing')
        if report.get('ir_mode', -1) != modes.get(name, 17):
            raise RuntimeError('Profile did not record the requested IR policy')
        if report.get('code_compare', -1) != compare_modes.get(name, -1):
            raise RuntimeError('Profile did not record requested exact comparison policy')
        if report.get('tlb_hash', -1) != hash_modes.get(name, -1):
            raise RuntimeError('Profile did not record requested TLB index policy')
        if name in inputs and report['input_sha256'] != hashlib.sha256(inputs[name].read_bytes()).hexdigest():
            raise RuntimeError('Profile did not use requested input route')
        if report['measurement']['first_virtual_us'] != args.start_us or report['measurement']['last_virtual_us'] != args.end_us:
            raise RuntimeError('Profile did not use requested guest window')
        row = dict(unsafe_code=report['unsafe_code'],leaf_features=report['leaf_features'],predicated_leaves=report['predicated_leaves'], runtime_footprint=report.get('runtime_footprint'), warmup_seconds=report['warmup_seconds'], execution_limits=report['execution_limits'], tlb_hash=report.get('tlb_hash', -1), input_sha256=report['input_sha256'], code_compare=report.get('code_compare', -1), name=label, build=str(build), ir_mode=report.get('ir_mode', -1),
                   wasm_sha256=report['wasm_sha256'], loader_sha256=report['loader_sha256'],
                   measurement=report['measurement'], cpu_time=report['cpu_time'])
        rows.append(row)
        (args.output / 'measurements.json').write_text(json.dumps(rows, indent=2) + '\n')
        print(json.dumps(row), flush=True)
