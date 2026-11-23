#!/usr/bin/env python3
"""Plots results/benchmark_raw.csv (produced by ./build-release/cma_benchmark)
into results/*.png. Reads only real measured data from the CSV - never
invents or interpolates numbers."""
import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parent.parent
CSV_PATH = ROOT / "results" / "benchmark_raw.csv"
OUT_DIR = ROOT / "results"


def load_rows(path: Path):
    rows = []
    with path.open() as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            rows.append(line)
    reader = csv.DictReader(rows)
    return list(reader)


def plot_throughput(rows, out_path: Path):
    workloads = sorted({r["workload"] for r in rows})
    allocators = sorted({r["allocator"] for r in rows})

    fig, ax = plt.subplots(figsize=(9, 5))
    width = 0.15
    x = range(len(workloads))
    for i, allocator in enumerate(allocators):
        values = []
        for workload in workloads:
            match = [r for r in rows if r["allocator"] == allocator and r["workload"] == workload]
            values.append(float(match[0]["throughput_ops_per_sec"]) if match else 0.0)
        offsets = [xi + (i - len(allocators) / 2) * width for xi in x]
        ax.bar(offsets, values, width=width, label=allocator)

    ax.set_xticks(list(x))
    ax.set_xticklabels(workloads)
    ax.set_ylabel("throughput (ops/sec)")
    ax.set_title("Allocator throughput by workload (measured, see results/benchmark_raw.csv)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def plot_latency(rows, out_path: Path):
    workloads = sorted({r["workload"] for r in rows})
    allocators = sorted({r["allocator"] for r in rows})

    fig, axes = plt.subplots(1, 2, figsize=(12, 5), sharey=True)
    for ax, metric, title in zip(axes, ["p50_latency_ns", "p99_latency_ns"], ["p50 latency (ns)", "p99 latency (ns)"]):
        width = 0.15
        x = range(len(workloads))
        for i, allocator in enumerate(allocators):
            values = []
            for workload in workloads:
                match = [r for r in rows if r["allocator"] == allocator and r["workload"] == workload]
                values.append(float(match[0][metric]) if match else 0.0)
            offsets = [xi + (i - len(allocators) / 2) * width for xi in x]
            ax.bar(offsets, values, width=width, label=allocator)
        ax.set_xticks(list(x))
        ax.set_xticklabels(workloads)
        ax.set_title(title)
    axes[0].set_ylabel("nanoseconds")
    axes[1].legend(fontsize=8, loc="upper right")
    fig.suptitle("Allocator latency by workload (measured, see results/benchmark_raw.csv)")
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def plot_peak_reserved(rows, out_path: Path):
    workloads = sorted({r["workload"] for r in rows})
    allocators = sorted({r["allocator"] for r in rows})

    fig, ax = plt.subplots(figsize=(9, 5))
    width = 0.15
    x = range(len(workloads))
    for i, allocator in enumerate(allocators):
        values = []
        for workload in workloads:
            match = [r for r in rows if r["allocator"] == allocator and r["workload"] == workload]
            values.append(float(match[0]["peak_reserved_bytes"]) / 1024.0 if match else 0.0)
        offsets = [xi + (i - len(allocators) / 2) * width for xi in x]
        ax.bar(offsets, values, width=width, label=allocator)

    ax.set_xticks(list(x))
    ax.set_xticklabels(workloads)
    ax.set_ylabel("peak bytes concurrently reserved (KiB)")
    ax.set_yscale("log")
    ax.set_title("Peak concurrently-reserved memory by workload (log scale, measured)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def main():
    if not CSV_PATH.exists():
        print(f"error: {CSV_PATH} not found - run ./build-release/cma_benchmark > results/benchmark_raw.csv first",
              file=sys.stderr)
        return 1
    rows = load_rows(CSV_PATH)
    OUT_DIR.mkdir(exist_ok=True)
    plot_throughput(rows, OUT_DIR / "throughput.png")
    plot_latency(rows, OUT_DIR / "latency.png")
    plot_peak_reserved(rows, OUT_DIR / "peak_reserved.png")
    print(f"wrote {OUT_DIR / 'throughput.png'}, {OUT_DIR / 'latency.png'}, {OUT_DIR / 'peak_reserved.png'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
