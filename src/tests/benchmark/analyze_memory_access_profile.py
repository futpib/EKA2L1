#!/usr/bin/env python3
"""Summarize paused memory-access captures; capacity estimates are not benchmarks."""
import argparse
import json
import math
from pathlib import Path

MIB = 1024 * 1024
BEGIN = 4 * MIB


def counts(profile):
    return {(r["asid"], bucket): reads + writes
            for r in profile["rows"] for bucket, reads, writes in r["bins"]}


def capacity(events, sizes):
    active, peak, final = set(), 0, 0
    for event in events:
        space = event["asid"]
        if event["kind"] == "arena":
            active.add(space)
        elif event["kind"] == "process-destroy":
            active.discard(space)
        final = sum(sizes.get(space, 64) for space in active)
        peak = max(peak, final)
    return {"peak_mib": peak, "final_mib": final}


def growth(events, first_runtime_event):
    sizes = {}
    copies = grows = runtime_grows = peak = 0
    for index, event in enumerate(events):
        if event["kind"] == "process-destroy":
            sizes.pop(event["asid"], None)
        elif event["kind"] == "create" and event["direct"]:
            need = (event["address"] + event["size"] - BEGIN) / MIB
            size = sizes.get(event["asid"], 4)
            while size < need:
                copies += size
                grows += 1
                runtime_grows += index >= first_runtime_event
                size *= 2
            sizes[event["asid"]] = size
        peak = max(peak, sum(sizes.values()))
    return {"growths": grows, "runtime_growths": runtime_grows,
        "copy_mib": copies, "peak_mib": peak, "final_mib": sum(sizes.values())}


def summarize(directory):
    profile = json.loads((directory / "profile-final.json").read_text())
    all_counts = counts(profile)
    runtime = {}
    windows = []
    first_runtime_event = len(profile["events"])
    for before in sorted(directory.glob("*-before.json")):
        label = before.name.removesuffix("-before.json")
        after = json.loads((directory / (label + "-after.json")).read_text())
        before_profile = json.loads(before.read_text())
        first_runtime_event = min(first_runtime_event, len(before_profile["events"]))
        before_counts = counts(before_profile)
        for key, count in counts(after).items():
            runtime[key] = runtime.get(key, 0) + count - before_counts.get(key, 0)
        windows.append(json.loads((directory / (label + ".json")).read_text()))
    needs = {}
    for event in profile["events"]:
        if event["kind"] == "create" and event["direct"]:
            end = event["address"] + event["size"] - BEGIN
            needs[event["asid"]] = max(needs.get(event["asid"], 0), math.ceil(end / MIB))
    rows = []
    live = {}
    for event in profile["events"]:
        key = event["asid"], event["address"]
        if event["kind"] == "create":
            live[key] = event
        elif event["kind"] in ("commit", "decommit") and key in live:
            live[key] = event
        elif event["kind"] == "destroy":
            live.pop(key, None)
    for row in profile["rows"]:
        space = row["asid"]
        conflicts = [e for (owner, _), e in live.items() if owner == space and e["local"]
            and e["committed"] and not e["direct"] and e["address"] < BEGIN + 64 * MIB
            and e["address"] + e["size"] > BEGIN]
        rows.append({"asid": space, "name": row["name"], "needed_mib": needs.get(space),
            "runtime_accesses": sum(n for (owner, _), n in runtime.items() if owner == space),
            "conflicting_separate_chunks": conflicts})
    def distribution(data):
        total = sum(data.values())
        local = sum(n for (_, b), n in data.items() if 4 <= b < 68)
        return {"accesses": total, "in_current_address_window": local,
            "address_histogram": [[bucket, sum(n for (_, b), n in data.items() if b == bucket)]
                for bucket in sorted({b for (_, b), n in data.items() if n})],
            "address_coverage_of_current_window_percent": {
                str(size): 100 * sum(n for (_, b), n in data.items() if 4 <= b < 4 + size) / local if local else 0
                for size in (4, 8, 12, 16, 20, 24, 32, 40, 48, 64)},
            "histogram": [sum(n for (_, b), n in data.items() if b == 4 + index) for index in range(64)]}
    rounded = {space: min(size for size in (4, 8, 12, 16, 20, 24, 32, 40, 48, 64) if size >= need)
               for space, need in needs.items()}
    result = {"game": directory.name, "runtime": distribution(runtime),
        "all_phases": distribution(all_counts), "processes": rows,
        "observed_required_window_mib": max(needs.values(), default=0),
        "current": capacity(profile["events"], {}),
        "fixed_48": capacity(profile["events"], {space: 48 for space in needs}),
        "rounded_per_process_oracle": capacity(profile["events"], rounded),
        "exact_per_process_oracle": capacity(profile["events"], needs),
        "doubling_growth_oracle": growth(profile["events"], first_runtime_event),
        "windows": [{k: window[k] for k in ("label", "guestSeconds", "frames", "errors", "assets")} for window in windows]}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    games = json.loads((Path(__file__).parent.parent / "wasm/games.json").read_text())
    result = {"limits": ["1 MiB start-address bins; read/write operations, not instruction fetches or host C++ copies",
        "Short fixed routes; not all levels or all future chunk requests",
        "Address coverage is not a hit-rate or a throughput measurement",
        "Per-process capacity oracle uses future allocations and is not an implementable online policy"],
        "games": [summarize(args.captures / game["id"]) for game in games]}
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    for index, game in enumerate(result["games"], 1):
        dist = game["runtime"]["address_coverage_of_current_window_percent"]
        print(index, game["game"], "runtime accesses", game["runtime"]["accesses"],
              "coverage 16/32/48", *(round(dist[str(size)], 3) for size in (16, 32, 48)),
              "required", game["observed_required_window_mib"], "MiB; capacity", game["current"],
              "oracle", game["rounded_per_process_oracle"])


if __name__ == "__main__":
    main()
