#!/usr/bin/env python3
"""Join diagnostic arena lifetimes with per-MiB resident-page counts."""

import argparse
import json
from pathlib import Path


MIB = 1 << 20
ARENA_BYTES = 48 * MIB
PAGE_BYTES = 4096


def prior_release_owners(allocation, releases):
    if 'sequence' not in allocation:
        return []
    begin = allocation['pointer']
    end = begin + ARENA_BYTES
    owners = [None] * (ARENA_BYTES // PAGE_BYTES)
    for release in sorted(releases, key=lambda row: row['sequence'], reverse=True):
        if release['sequence'] >= allocation['sequence']:
            continue
        first = max(begin, release['pointer'])
        last = min(end, release['pointer'] + release['usable'])
        if first >= last:
            continue
        for page in range((first - begin) // PAGE_BYTES, (last - begin + PAGE_BYTES - 1) // PAGE_BYTES):
            if owners[page] is None:
                owners[page] = release['group']
    counts = {}
    for owner in owners:
        if owner is not None:
            counts[owner] = counts.get(owner, 0) + 1
    return [{'group': group, 'overlap_mib': pages * PAGE_BYTES / MIB}
            for group, pages in sorted(counts.items(), key=lambda item: -item[1])]


def summarize(allocations, guest_memory):
    names = {row['asid']: row['name'] for row in guest_memory['rows']}
    arenas = {}
    chunks = {}
    for event in guest_memory['events']:
        asid = event['asid']
        kind = event['kind']
        if kind == 'arena':
            assert asid not in arenas
            arenas[asid] = {'host': event['host'], 'capacity': 0}
        elif kind == 'arena-grow':
            arena = arenas[asid]
            assert arena['host'] == event['host']
            arena['capacity'] = event['committed']
        elif kind == 'process-destroy':
            arenas.pop(asid, None)
            for key in list(chunks):
                if key[0] == asid:
                    del chunks[key]
        elif event['direct']:
            key = asid, event['address']
            if kind == 'create':
                chunks[key] = event
            elif kind in ('commit', 'decommit') and key in chunks:
                chunks[key] = event
            elif kind == 'destroy':
                chunks.pop(key, None)

    resident = {row['pointer']: row for row in allocations['residency']['allocations']
                if row['requested'] == ARENA_BYTES}
    assert len(arenas) == len(resident)
    rows = []
    for asid, arena in sorted(arenas.items()):
        allocation = resident.pop(arena['host'])
        buckets = allocation['resident_page_counts_by_mib']
        capacity = arena['capacity']
        assert len(buckets) == 48 and capacity % MIB == 0
        assert sum(buckets) * PAGE_BYTES == allocation['whole_page_resident_bytes']
        prefix = sum(buckets[:capacity // MIB]) * PAGE_BYTES
        tail = sum(buckets[capacity // MIB:]) * PAGE_BYTES
        rows.append({'asid': asid, 'process': names.get(asid, ''),
                     'host': arena['host'], 'capacity_mib': capacity // MIB,
                     'guest_committed_mib': sum(event['committed'] for (owner, _), event in chunks.items()
                                                if owner == asid) / MIB,
                     'resident_prefix_mib': prefix / MIB,
                     'resident_tail_mib': tail / MIB,
                     'resident_total_mib': (prefix + tail) / MIB,
                     'resident_pages_by_mib': buckets,
                     'prior_release_owners': prior_release_owners(
                         allocation, allocations.get('allocation', {}).get('released_large_allocations', []))})
    assert not resident
    assert sum(row['capacity_mib'] for row in rows) * MIB == allocations['memory']['arena_initialized_bytes']
    assert len(rows) * ARENA_BYTES == allocations['memory']['arena_bytes']
    return {'rows': rows, 'totals': {
        'arenas': len(rows), 'reserved_mib': len(rows) * 48,
        'capacity_mib': sum(row['capacity_mib'] for row in rows),
        'guest_committed_mib': sum(row['guest_committed_mib'] for row in rows),
        'resident_prefix_mib': sum(row['resident_prefix_mib'] for row in rows),
        'resident_tail_mib': sum(row['resident_tail_mib'] for row in rows),
        'resident_total_mib': sum(row['resident_total_mib'] for row in rows)}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('allocations', type=Path)
    parser.add_argument('guest_memory', type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(json.loads(args.allocations.read_text()),
                               json.loads(args.guest_memory.read_text())), indent=2))


if __name__ == '__main__':
    main()
