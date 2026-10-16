#!/usr/bin/env python3
"""Summarize browser file traces and replay optimistic read-only block caches."""

import argparse
import collections
import csv
import gzip
import json
from pathlib import Path


OPCODES = {
    16: "mkdir", 17: "rmdir", 18: "parse", 19: "delete", 20: "rename",
    21: "replace", 22: "entry", 30: "file_open", 31: "file_create",
    28: "file_close", 32: "file_replace", 34: "file_read", 35: "file_write", 38: "file_seek",
    40: "file_size", 49: "dir_open", 50: "dir_read", 51: "dir_read_packed",
    67: "is_file_in_rom", 70: "read_file_section",
}
BLOCK = 4096
CAPACITIES_KIB = (0, 32, 64, 128, 256, 512, 1024, 2048, 4096)


def summary(rows, key):
    grouped = collections.defaultdict(lambda: [0, 0, 0])
    for row in rows:
        item = grouped[key(row)]
        item[0] += 1
        item[1] += int(row["duration_us"])
        item[2] += int(row.get("actual_length", row.get("bytes", 0)) or 0)
    return [dict(name=name, calls=value[0], duration_ms=round(value[1] / 1000, 2),
                 bytes=value[2]) for name, value in sorted(grouped.items(),
                 key=lambda pair: pair[1][1], reverse=True)]


def cache_replay(rows, host=False):
    results = {}
    reads = []
    for row in rows:
        if host:
            if row["op"] not in ("read_blob", "read_access"):
                continue
            size = int(row["bytes"])
        else:
            if int(row["op"]) not in (34, 70) or row["rom_file"] != "0":
                continue
            size = int(row["actual_length"])
        path = row["path"].lower()
        offset = int(row["offset"])
        if not path or path.startswith("id:") or offset < 0 or offset >= 2**64 - 1 or size <= 0:
            continue
        reads.append((path, offset, size))
    for capacity_kib in CAPACITIES_KIB:
        capacity = capacity_kib * 1024 // BLOCK
        blocks = collections.OrderedDict()
        hits = misses = fully_cached_calls = 0
        for path, offset, size in reads:
            requested = range(offset // BLOCK, (offset + size - 1) // BLOCK + 1)
            if capacity and all((path, block) in blocks for block in requested):
                fully_cached_calls += 1
            for block in requested:
                key = (path, block)
                if key in blocks:
                    hits += 1
                    blocks.move_to_end(key)
                else:
                    misses += 1
                    if capacity:
                        blocks[key] = None
                        if len(blocks) > capacity:
                            blocks.popitem(last=False)
        results[str(capacity_kib)] = dict(page_hits=hits, page_misses=misses,
            miss_bytes=misses * BLOCK, page_hit_rate=round(hits / (hits + misses), 4)
            if hits + misses else None, fully_cached_calls=fully_cached_calls,
            call_hit_rate=round(fully_cached_calls / len(reads), 4) if reads else None)
    return dict(reads=len(reads), block_bytes=BLOCK, capacities_kib=results)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("guest_tsv", type=Path)
    parser.add_argument("output_json", type=Path)
    parser.add_argument("--opfs-json", type=Path)
    args = parser.parse_args()
    def open_trace(path):
        return gzip.open(path, "rt", newline="") if path.suffix == ".gz" else path.open(newline="")

    with open_trace(args.guest_tsv) as stream:
        guest = list(csv.DictReader(stream, delimiter="\t"))
    report = {
        "guest_calls": len(guest),
        "guest_by_opcode": summary(guest, lambda row: OPCODES.get(int(row["op"]), row["op"])),
        "guest_by_dll": summary(guest, lambda row: row["module"]),
        "guest_by_process": summary(guest, lambda row: row["process"]),
        "guest_by_path": summary(guest, lambda row: row["path"]),
        "guest_sync": summary(guest, lambda row: "sync" if row["guest_sync"] == "1" else "async"),
        "physical_read_cache": cache_replay(guest),
        "guest_slowest": sorted(guest, key=lambda row: int(row["duration_us"]), reverse=True)[:30],
    }
    if args.opfs_json:
        with open_trace(args.opfs_json) as stream:
            opfs = json.load(stream)
        report["opfs_calls"] = len(opfs)
        report["opfs_by_operation"] = summary(opfs, lambda row: row["op"])
        report["opfs_by_path"] = summary(opfs, lambda row: row["path"])
        report["opfs_read_cache"] = cache_replay(opfs, host=True)
        report["opfs_slowest"] = sorted(opfs, key=lambda row: int(row["duration_us"]), reverse=True)[:30]
    args.output_json.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
