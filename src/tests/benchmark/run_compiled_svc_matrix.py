#!/usr/bin/env python3
"""Compare production syscall boundaries with native DynCom, including callbacks."""
import argparse
import hashlib
import json
from itertools import product
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native', type=Path)
    parser.add_argument('wasm', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--ir-policies', nargs='+', type=int, choices=[17], default=[17])
    parser.add_argument('--entry-budget-modes', nargs='+', type=int, choices=[0,2], default=[0])
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for mode, policy, entry_budget in product((0, 3), args.ir_policies, args.entry_budget_modes):
        rows, hashes = {}, {}
        for kind, command in [('native', [str(args.native.resolve())]),
                              ('wasm', ['node', str(args.wasm.resolve())])]:
            path = args.output / f'{mode}-ir{policy}-entry{entry_budget}-{kind}.log'
            with path.open('w') as log:
                subprocess.run(command + ['--compiled-svc', f'--ir-policy={policy}',
                                           f'--unsafe-code={mode}', f'--aot-verify={int(args.verify)}',
                                           f'--entry-budget-mode={entry_budget}'],
                               stdout=log, stderr=subprocess.STDOUT,
                               check=True, timeout=300)
            lines = path.read_text().splitlines()
            for marker in (f'PROBE_UNSAFE_CODE {mode}', f'PROBE_POLICY {policy}',
                           f'PROBE_ENTRY_BUDGET {entry_budget}', f'PROBE_AOT_VERIFY {int(args.verify)}'):
                if lines.count(marker) != 1:
                    raise ValueError(f'{path}: missing or duplicate policy marker {marker}')
            rows[kind] = [json.loads(line[6:]) for line in lines if line.startswith('FAULT ')]
            if [row['id'] for row in rows[kind]] != list(range(10368)):
                raise ValueError(f'{path}: incomplete or duplicate syscall cases')
            hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        differences = [{'native': n, 'wasm': w}
                       for n, w in zip(rows['native'], rows['wasm']) if n != w]
        results.append(dict(unsafe_code=mode, cases=10368, ir_policy=policy, entry_budget=entry_budget, verify=args.verify,
                            all_fields_match=10368 - len(differences),
                            differences=differences, log_hashes=hashes))
        (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        if differences:
            raise RuntimeError(f'Mode {mode}: {len(differences)} syscall mismatches')
        print(f'PASS mode {mode}, IR {policy}, entry {entry_budget}, verify {args.verify}: 10368 exact native/WASM syscall comparisons', flush=True)


if __name__ == '__main__':
    main()
