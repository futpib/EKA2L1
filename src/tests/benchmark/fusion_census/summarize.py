#!/usr/bin/env python3
"""Summarize executed guest calls; region handoffs have a separate denominator."""
import argparse
import collections
import json
from pathlib import Path

CALL_KINDS = {
    'arm_leaf_call', 'arm_prefix_call', 'arm_indirect_call',
    'arm_indirect_miss_call', 'arm_dispatch_call', 'arm_sibling_call',
    'thumb_fused_call', 'thumb_prefix_call', 'thumb_dispatch_call',
    'thumb_sibling_call', 'arm_interpreted_call', 'thumb_interpreted_call',
}
OTHER_KINDS = {'arm_leaf_return', 'arm_indirect_return', 'thumb_fused_branch'}


def summarize(captures):
    totals = collections.Counter()
    sites = collections.Counter()
    entries = returns = 0
    for capture in captures:
        assert capture['region_entries'] == capture['region_returns'], 'Capture is not at a completed region boundary'
        entries += capture['region_entries']
        returns += capture['region_returns']
        for row in capture['sites']:
            assert row['kind'] in CALL_KINDS | OTHER_KINDS, row['kind']
            assert isinstance(row['count'], int) and row['count'] > 0
            totals[row['kind']] += row['count']
            sites[row['kind'], row['pc'], row['target']] += row['count']
    calls = sum(totals[kind] for kind in CALL_KINDS)
    assert calls > 0
    complete = totals['arm_leaf_return'] + totals['arm_indirect_return']
    early = totals['arm_leaf_call'] + totals['arm_indirect_call'] - complete
    assert early >= 0, 'Fused returns exceed entries in a paused-boundary capture'
    prefix = totals['arm_prefix_call'] + totals['thumb_prefix_call']
    sibling = totals['arm_sibling_call'] + totals['thumb_sibling_call']
    partial = prefix + totals['thumb_fused_call'] + early + sibling
    interpreted = totals['arm_interpreted_call'] + totals['thumb_interpreted_call']
    dispatched = (totals['arm_dispatch_call'] + totals['arm_indirect_miss_call']
                  + totals['thumb_dispatch_call'] + interpreted)
    assert complete + partial + dispatched == calls
    return {
        'call_count': calls,
        'complete_fused_calls': complete,
        'partial_fused_calls': partial,
        'ordinary_dispatch_calls': dispatched,
        'percent': {key: 100 * value / calls for key, value in
                    [('complete', complete), ('partial', partial), ('ordinary', dispatched)]},
        'complete_indirect_calls': totals['arm_indirect_return'],
        'complete_indirect_percent': 100 * totals['arm_indirect_return'] / calls,
        'early_fused_exits': early,
        'prefix_calls': prefix,
        'thumb_entry_only_calls': totals['thumb_fused_call'],
        'sibling_calls': sibling,
        'interpreted_calls': interpreted,
        'region_entries': entries,
        'region_returns': returns,
        'thumb_fused_noncall_branches': totals['thumb_fused_branch'],
        'totals': dict(sorted(totals.items())),
        'top_calls': [dict(kind=kind, pc=pc, target=target, count=count)
                      for (kind, pc, target), count in sites.most_common()
                      if kind in CALL_KINDS][:30],
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', nargs='+', type=Path, help='Paths to fusion-census.json')
    args = parser.parse_args()
    print(json.dumps(summarize([json.loads(p.read_text()) for p in args.captures]), indent=2))
