#!/usr/bin/env python3
"""Replay or time the four memory implementations serially on fixed guest work."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import wave

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
p.add_argument('phase', choices=['replays', 'timings'])
p.add_argument('--build', type=Path, required=True)
p.add_argument('--snakes-assets', type=Path, required=True)
p.add_argument('--sky-assets', type=Path, required=True)
p.add_argument('--reference-root', type=Path, required=True)
p.add_argument('--modes', nargs='+', type=int, choices=range(4), default=[0,1,2,3])
p.add_argument('--games', nargs='+', choices=['standard','combat'], default=['standard','combat'])
p.add_argument('--rounds', type=int, default=2)
p.add_argument('--window-us', type=int, default=0, help='Override the measured guest window; zero uses 4/6 seconds')
p.add_argument('--activation-lead-us', type=int, default=1000, help='Guest time to warm identity memory before measurement')
p.add_argument('--frames', type=int, default=60, help='Replay prefix length, up to 60 native reference frames')
a = p.parse_args()
if a.rounds < 1 or a.window_us < 0 or not 0 < a.activation_lead_us < 21000000 or not 1 <= a.frames <= 60:
    p.error('Invalid rounds, window, activation lead or frame count')
a.output.mkdir(parents=True, exist_ok=False)
repo = Path(__file__).resolve().parents[3]
names = ['tlb','allocation','identity','flat-pages']
rows = []
build_hashes = None
for game in a.games:
    start,end,work = (21000000,25000000,644728231) if game=='standard' else (42000000,48000000,2171043925)
    if a.window_us:end,work=start+a.window_us,None
    reference=a.reference_root/f'replay-{game}'
    if a.phase=='replays' and a.frames<60:
        # Exact prefix of the existing native oracle, including audio from boot.
        prefix=a.output/f'native-{game}-prefix';prefix.mkdir()
        lines=(reference/'frames.jsonl').read_text().splitlines()[:a.frames]
        last=json.loads(lines[-1])['virtual_us']
        (prefix/'frames.jsonl').write_text('\n'.join(lines)+'\n')
        for i in range(a.frames):(prefix/f'frame-{i:04d}.png').symlink_to((reference/f'frame-{i:04d}.png').resolve())
        events=[line for line in (reference/'audio.jsonl').read_text().splitlines() if json.loads(line)['virtual_us']<=last]
        (prefix/'audio.jsonl').write_text('\n'.join(events)+'\n')
        (prefix/'audio-backend.json').symlink_to((reference/'audio-backend.json').resolve())
        with wave.open(str(reference/'audio.wav'),'rb') as source, wave.open(str(prefix/'audio.wav'),'wb') as target:
            # clocked_audio_driver::finish pads the final unserviced partial
            # 10 ms quantum with silence, unlike a later capture's PCM prefix.
            count=last*48000//1000000;complete=count//480*480
            target.setparams(source.getparams())
            target.writeframes(source.readframes(complete)+bytes((count-complete)*4))
        reference=prefix
    frame_journal=None
    assets = a.snakes_assets if game=='standard' else a.sky_assets
    route = repo/'src/tests/benchmark'/('snakes.input' if game=='standard' else 'sky-force-combat.input')
    for repetition in range(1 if a.phase=='replays' else a.rounds):
        order = a.modes if repetition%2==0 else list(reversed(a.modes))
        for mode in order:
            name=f'{game}-{repetition}-{names[mode]}';out=a.output/name
            config = dict(EKA2L1_BENCHMARK_AOT='5',EKA2L1_CODE_COMPARE='2',EKA2L1_PREDICATED_LEAVES='1',
                EKA2L1_LEAF_FEATURES='128',EKA2L1_UNSAFE_CODE='3',EKA2L1_SHARED_AUDIO='1',
                EKA2L1_ARM_MEMORY='0',EKA2L1_ARM_EXCLUSIVE='0',EKA2L1_AOT_IR_MODE='17',
                EKA2L1_HOTPATH='2',EKA2L1_THUMB_MEMORY='1',EKA2L1_MEMORY_IMPL=str(mode),
                EKA2L1_WASM_BUILD_DIR=str(a.build.resolve()),EKA2L1_PROFILE_DETAIL='0',EKA2L1_CHROME_TRACE='off')
            if mode==2:config['EKA2L1_MEMORY_ACTIVATE_US']=str(start-a.activation_lead_us)
            if game=='combat':
                config.update(EKA2L1_APP_UID='0xa020d913',EKA2L1_ASSET_MANIFEST=str(repo/'src/tests/benchmark/sky-force-assets.json'))
            if a.phase=='replays':
                args=['node',str(repo/'src/tests/wasm/benchmark.ts'),str(assets.resolve()),str(out.resolve()),str(a.frames),str(route),str(start)]
            else:
                config.update(EKA2L1_GPU='hardware',EKA2L1_PROFILE_START_US=str(start),EKA2L1_PROFILE_INPUT=str(route))
                args=['node',str(repo/'src/tests/wasm/profile.ts'),str(assets.resolve()),str(out.resolve()),'1','0',str(end)]
            env={k:v for k,v in os.environ.items() if not k.startswith('EKA2L1_')};env.update(config)
            command=dict(args=args,env=config,cwd=str(repo))
            (a.output/(name+'-command.json')).write_text(json.dumps(command,indent=2)+'\n')
            print('START',name,flush=True);beg=time.monotonic()
            with (a.output/(name+'.log')).open('w') as log:
                subprocess.run(args,cwd=repo,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=1800,check=True)
            row=dict(game=game,mode=mode,name=name,elapsed_seconds=time.monotonic()-beg,command=command)
            report=json.loads((out/'report.json').read_text())
            assert report['memory_impl']==mode
            hashes=(report['wasm_sha256'],report['loader_sha256'])
            if build_hashes is None:build_hashes=hashes
            assert hashes==build_hashes, 'Build changed during the campaign'
            if mode==2:assert start-a.activation_lead_us<=report['memory_impl_stats']['activated_us']<start
            row['report']=report
            if a.phase=='replays':
                comparison=subprocess.run(['python3',str(repo/'src/tests/benchmark/compare.py'),str(reference),str(out)],capture_output=True,text=True)
                (a.output/(name+'-compare.log')).write_text(comparison.stdout+comparison.stderr)
                comparison.check_returncode();row['comparison']=json.loads(comparison.stdout)
            else:
                measured=report['measurement']
                row['instructions']=measured['last_instructions']-measured['first_instructions']
                if work is None:work=row['instructions']
                assert row['instructions']==work,report
                assert report['purpose']=='throughput' and report['chrome_trace']['scope']=='off' and not report['sampling']
                journal=(out/'frames.jsonl').read_bytes()
                if frame_journal is None:frame_journal=journal
                assert journal==frame_journal, 'Presentation journal changed between implementations'
                cpu=report['cpu_time']
                # Chrome may retire an idle pool thread during a long run.
                # Keep that churn in the report; process totals remain complete,
                # and the busiest matched thread has an independently valid delta.
                assert cpu['renderer_complete'] and not cpu['thread_errors'],cpu
                assert cpu['busiest_renderer_thread']['status']=='matched',cpu
                assert all(t['name'].startswith('ThreadPool') for t in cpu['missing_threads']),cpu
            rows.append(row)
            (a.output/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
            print('PASS',name,round(row['elapsed_seconds'],2),row.get('report',{}).get('measurement',row.get('comparison')),flush=True)
