#!/usr/bin/env python3
"""Render a saved memory-access summary; requires matplotlib and numpy."""
import argparse
import json
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import LogNorm
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("summary")
parser.add_argument("output")
args = parser.parse_args()
with open(args.summary) as source:
    games = json.load(source)["games"]
fig, axes = plt.subplots(1, 2, figsize=(13, 4.8), sharey=True, layout="constrained")
for axis, phase, title in zip(axes, ["all_phases", "runtime"],
        ["Installation, startup and gameplay", "Gameplay / client navigation windows"]):
    values = np.array([game[phase]["histogram"] for game in games], dtype=float)
    values = values / np.maximum(values.sum(axis=1, keepdims=True), 1) * 100
    plot = axis.imshow(np.ma.masked_less_equal(values, 0), aspect="auto",
        norm=LogNorm(vmin=.0001, vmax=100), cmap="magma", extent=(0, 64, len(games)-.5, -.5))
    axis.set_title(title)
    axis.set_xticks([0, 8, 16, 24, 32, 40, 48, 56, 64])
    axis.set_xlabel("MiB offset from guest 0x00400000")
    for size in [16, 32, 48]:
        axis.axvline(size, color="#4ac6ff", ls="--", lw=.8)
axes[0].set_yticks(range(len(games)), [str(index+1)+". "+game["game"] for index, game in enumerate(games)])
fig.colorbar(plot, ax=axes, label="% of data accesses within the current 64 MiB address window")
fig.suptitle("Guest data-access distribution, 1 MiB bins — instrumented runs, not throughput benchmarks")
fig.savefig(args.output, dpi=160)
