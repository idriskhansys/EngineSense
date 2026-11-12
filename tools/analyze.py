#!/usr/bin/env python3
"""Statistical analysis of the Monte-Carlo benchmark output (std-dev, percentiles, control charts,
threshold tuning). Inputs: CSVs written by `enginesense_bench` (default docs/data).
Outputs: docs/results.md and PNG figures in docs/figures. Needs numpy + matplotlib."""
import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
BLUE, ORANGE, AQUA, MAGENTA = "#2a78d6", "#eb6834", "#1baf7a", "#e87ba4"
INK, MUTED, SURFACE = "#0b0b0b", "#52514e", "#fcfcfb"
SERIES = [BLUE, ORANGE, AQUA, MAGENTA]

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": MUTED, "axes.labelcolor": INK, "text.color": INK, "xtick.color": MUTED, "ytick.color": MUTED,
    "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True, "grid.color": "#e6e5e1",
    "grid.linewidth": 0.6, "font.size": 9, "axes.titlesize": 10, "axes.titleweight": "bold", "axes.titlelocation": "left",
})


def read(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def pct(a, q):
    return float(np.percentile(a, q))


def latency_table(rows):
    by = defaultdict(list)
    meta = {}
    for r in rows:
        by[r["scenario"]].append(r)
        meta[r["scenario"]] = (r["requirement"], float(r["budget_ms"]))
    out = []
    for name, rs in by.items():
        det = [r for r in rs if r["detected"] == "1"]
        lat = np.array([float(r["latency_ms"]) for r in det])
        budget = meta[name][1]
        sd = float(lat.std(ddof=1)) if len(lat) > 1 else 0.0
        cpk = (budget - lat.mean()) / (3 * sd) if sd > 0 else float("inf")
        n = len(rs)
        out.append(dict(name=name, req=meta[name][0], n=n, detected=len(det), mean=lat.mean(), sd=sd,
                        p50=pct(lat, 50), p95=pct(lat, 95), p99=pct(lat, 99), max=lat.max(), budget=budget,
                        margin=budget - lat.max(), cpk=cpk,
                        fa=sum(int(r["false_alarms"]) for r in rs), unexp=sum(int(r["unexpected"]) for r in rs),
                        det_lb=0.05 ** (1.0 / n) if len(det) == n else float("nan")))
    return out


def fig_latency(tab, rows, path):
    names = [t["name"] for t in tab]
    fig, ax = plt.subplots(figsize=(9, 5))
    data = []
    for t in tab:
        data.append([float(r["latency_ms"]) / t["budget"] * 100 for r in rows if r["scenario"] == t["name"] and r["detected"] == "1"])
    bp = ax.boxplot(data, orientation="horizontal", patch_artist=True, widths=0.55, showfliers=True,
                    medianprops=dict(color=INK, linewidth=1.5), flierprops=dict(markersize=3, markerfacecolor=MUTED, markeredgecolor=MUTED))
    for p in bp["boxes"]:
        p.set(facecolor=BLUE, edgecolor=SURFACE, alpha=0.9)
    ax.axvline(100, color=ORANGE, linewidth=2)
    ax.text(99, 0.02, "requirement budget (100 %)", color=ORANGE, fontsize=8, va="bottom", ha="right", transform=ax.get_xaxis_transform())
    ax.set_yticklabels([f"{t['name']}  [{t['budget']:.0f} ms]" for t in tab])
    ax.invert_yaxis()
    ax.set_xlabel("detection latency as % of requirement budget (200 trials each; box = IQR, whiskers 1.5 IQR)")
    ax.set_xlim(0, 110)
    ax.set_title("Every scenario detects inside its budget - latency relative to requirement")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def imr_limits(x):
    x = np.asarray(x, float)
    mr = np.abs(np.diff(x))
    mrbar = mr.mean()
    c = x.mean()
    return c, c - 2.66 * mrbar, c + 2.66 * mrbar  # 2.66 = 3 / d2(2), standard I-MR constants


def fig_control(rows, path):
    picks = [("coolant_overtemp", BLUE), ("et1_dropout", ORANGE), ("bus_overload", AQUA), ("ecu_dtc", MAGENTA)]
    fig, axes = plt.subplots(2, 2, figsize=(10, 6))
    for ax, (name, col) in zip(axes.ravel(), picks):
        rs = [r for r in rows if r["scenario"] == name and r["detected"] == "1"]
        y = [float(r["latency_ms"]) for r in rs]
        c, lcl, ucl = imr_limits(y)
        budget = float(rs[0]["budget_ms"])
        x = np.arange(1, len(y) + 1)
        ax.plot(x, y, color=col, linewidth=1.2, marker="o", markersize=2.5)
        ax.axhline(c, color=MUTED, linewidth=1)
        for lim in (lcl, ucl):
            ax.axhline(lim, color=MUTED, linewidth=1, linestyle=(0, (4, 3)))
        ooc = [i for i, v in enumerate(y) if v > ucl or v < lcl]
        ax.scatter(np.array(ooc) + 1, [y[i] for i in ooc], s=28, facecolors="none", edgecolors=INK, zorder=3)
        ax.set_title(f"{name}: mean {c:.0f} ms, UCL {ucl:.0f}, LCL {max(lcl, 0):.0f}, budget {budget:.0f}")
        ax.set_xlabel("trial (seed)")
        ax.set_ylabel("latency [ms]")
        ax.set_ylim(0, budget * 1.05)
        ax.axhline(budget, color=ORANGE, linewidth=2)
    fig.suptitle("Individuals control charts of detection latency (centre line = mean, dashed = +-3 sigma from moving range, orange = requirement)",
                 x=0.01, ha="left", fontsize=9, color=MUTED)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def fig_threshold(rows, path):
    by = defaultdict(list)
    other = {}
    for r in rows:
        hours = int(r["runs"]) * float(r["minutes_per_run"]) / 60
        ns = float(r["noise_scale"])
        by[ns].append((float(r["threshold_c"]), int(r["overtemp_false_alarms"]) / hours))
        other[ns] = int(r["other_false_alarms"]) / hours  # independent of the coolant limit
    fig, (ax, bx) = plt.subplots(1, 2, figsize=(11, 4.5), gridspec_kw={"width_ratios": [3, 2]})
    for col, (ns, pts) in zip(SERIES, sorted(by.items())):
        pts.sort()
        ax.plot([p[0] for p in pts], [max(p[1], 0.05) for p in pts], marker="o", markersize=4, color=col,
                label=f"noise x{ns:g} (sigma {0.2 * ns:.1f} degC)")
    ax.axhline(0.05, color=MUTED, linewidth=0.8)
    ax.text(90.1, 0.06, "0 observed (drawn at floor)", color=MUTED, fontsize=7)
    ax.axvline(105, color=ORANGE, linewidth=2)
    ax.text(104.8, 0.93, "shipped limit 105 degC", color=ORANGE, fontsize=8, ha="right", transform=ax.get_xaxis_transform())
    ax.set_yscale("log")
    ax.set_xlabel("coolant over-temperature threshold [degC]")
    ax.set_ylabel("over-temperature false alarms per hour")
    ax.set_title("The limit itself tolerates large noise")
    ax.legend(frameon=False, fontsize=8, loc="upper right", bbox_to_anchor=(0.9, 0.85))
    ks = sorted(other)
    bx.bar([f"x{k:g}" for k in ks], [other[k] for k in ks], color=ORANGE, width=0.55)
    for i, k in enumerate(ks):
        bx.text(i, other[k] + 5, f"{other[k]:.0f}", ha="center", fontsize=8)
    bx.set_xlabel("sensor noise scale")
    bx.set_ylabel("false alarms per hour, all other rules")
    bx.set_title("Rate-of-change rules break first")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def fig_confirm(conf_rows, glitch_rows, path):
    by = defaultdict(list)
    for r in conf_rows:
        if r["detected"] == "1":
            by[int(r["confirm_samples"])].append(float(r["latency_ms"]))
    ns = sorted(by)
    fig, (a, b) = plt.subplots(1, 2, figsize=(10, 4))
    mean = [np.mean(by[n]) for n in ns]
    p95 = [pct(by[n], 95) for n in ns]
    a.plot(ns, mean, marker="o", color=BLUE, label="mean")
    a.plot(ns, p95, marker="o", color=ORANGE, label="p95")
    a.axhline(2500, color=MUTED, linestyle=(0, (4, 3)))
    a.text(1, 2560, "REQ-LAT-001 budget 2500 ms", color=MUTED, fontsize=8)
    a.set_xticks(ns)
    a.set_xlabel("consecutive samples required (1 Hz signal)")
    a.set_ylabel("detection latency [ms]")
    a.set_title("Latency cost of confirmation")
    a.legend(frameon=False)
    g = [(int(r["confirm_samples"]), 100 * int(r["false_overtemp"]) / int(r["trials"])) for r in glitch_rows]
    b.bar([x for x, _ in g], [y for _, y in g], color=BLUE, width=0.55)
    for x, y in g:
        b.text(x, y + 1, f"{y:.0f} %", ha="center", fontsize=8)
    b.set_xticks([x for x, _ in g])
    b.set_xlabel("consecutive samples required")
    b.set_ylabel("single-reading glitches raised as fault [%]")
    b.set_ylim(0, 105)
    b.set_title("Benefit: immunity to one-sample glitches")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=str(ROOT / "docs" / "data"))
    ap.add_argument("--figures", default=str(ROOT / "docs" / "figures"))
    ap.add_argument("--results", default=str(ROOT / "docs" / "results.md"))
    a = ap.parse_args()
    data, figs = Path(a.data), Path(a.figures)
    figs.mkdir(parents=True, exist_ok=True)

    lat = read(data / "latency.csv")
    tab = latency_table(lat)
    fig_latency(tab, lat, figs / "latency_vs_budget.png")
    fig_control(lat, figs / "latency_control_charts.png")

    md = ["# Results", "", "_Generated by `tools/analyze.py` from the CSVs in `docs/data/` "
          "(`enginesense_bench latency|nominal|sweep`). Re-run to refresh._", "",
          "## Detection latency (200 seeds per scenario, 60 s runs, injection at 20 s + random phase)", "",
          "| Scenario | Req | Detected | mean ms | sigma ms | p50 | p95 | p99 | max | budget | margin ms | Cpk | false alarms | unexpected |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for t in tab:
        md.append(f"| {t['name']} | {t['req']} | {t['detected']}/{t['n']} | {t['mean']:.0f} | {t['sd']:.0f} | {t['p50']:.0f} | "
                  f"{t['p95']:.0f} | {t['p99']:.0f} | {t['max']:.0f} | {t['budget']:.0f} | {t['margin']:.0f} | {t['cpk']:.1f} | {t['fa']} | {t['unexp']} |")
    n = tab[0]["n"]
    md += ["", f"One-sided 95 % lower bound on the detection rate with {n}/{n} detections: **{100 * 0.05 ** (1 / n):.2f} %** "
           "(per scenario). Cpk = (budget - mean) / (3 sigma): how many 3-sigma widths of margin the requirement leaves.", "",
           "![latency vs budget](figures/latency_vs_budget.png)", "",
           "### Control charts", "",
           "Individuals charts (limits from the average moving range, 2.66 x MR-bar) show whether detection latency is stable "
           "across seeds or drifts / has special causes. Dropout latencies are uniformly distributed (the loss happens at a random "
           "phase of the message period) so they are bounded rather than bell shaped.", "",
           "![control charts](figures/latency_control_charts.png)", ""]

    nom = read(data / "nominal.csv")
    minutes = sum(float(r["minutes"]) for r in nom)
    fa = sum(int(r["false_alarms"]) for r in nom)
    frames = sum(int(r["frames"]) for r in nom)
    md += ["## False alarms on fault-free traffic", "",
           f"{len(nom)} runs x {nom[0]['minutes']} min = **{minutes:.0f} simulated minutes** ({frames / 1e6:.1f} M frames): "
           f"**{fa} false alarms**. Rule of three: with 0 events the 95 % upper bound on the false-alarm rate is "
           f"{3 / (minutes / 60):.2f} per hour.", ""]

    thr = data / "sweep_threshold.csv"
    if thr.exists() and thr.stat().st_size > 0:
        rows = read(thr)
        fig_threshold(rows, figs / "threshold_tuning.png")
        by = defaultdict(list)
        other = {}
        for r in rows:
            ns = float(r["noise_scale"])
            by[ns].append((float(r["threshold_c"]), int(r["overtemp_false_alarms"])))
            other[ns] = int(r["other_false_alarms"])
        hours = int(rows[0]["runs"]) * float(rows[0]["minutes_per_run"]) / 60
        md += ["## Threshold tuning and noise tolerance", "",
               f"Nominal coolant peaks at about 90 degC. Sweep: {rows[0]['runs']} runs x {rows[0]['minutes_per_run']} min per point "
               f"({hours:.1f} h), sensor noise scaled 1x-12x. Over-temperature false alarms are counted separately from every other rule.", "",
               "| noise x | sigma degC | lowest limit with 0 over-temp false alarms (any higher limit also 0) | other-rule false alarms per hour |",
               "|---|---|---|---|"]
        for ns, pts in sorted(by.items()):
            ok = [t for t, f in sorted(pts) if all(ff == 0 for tt, ff in pts if tt >= t)]
            lowest = f"{min(ok):.0f} degC" if ok else "none in sweep (90-106)"
            md.append(f"| {ns:g} | {0.2 * ns:.1f} | {lowest} | {other[ns] / hours:.0f} |")
        md += ["", "**Finding:** the over-temperature limit is robust - even at 12x noise only the lowest tested limit (90 degC) fires - "
               "but the *rate-of-change* rules (limits of 3-5 degC/s, only ~10 sigma from nominal noise) start producing false alarms at about "
               "4x noise. Noise immunity is therefore bounded by the rate limits, not the thresholds. Corrective action: see "
               "[technical_report.md](technical_report.md#lessons-learned).", "",
               "![threshold tuning](figures/threshold_tuning.png)", ""]

    cf, gl = data / "sweep_confirm.csv", data / "sweep_glitch.csv"
    if cf.exists() and gl.exists() and cf.stat().st_size and gl.stat().st_size:
        fig_confirm(read(cf), read(gl), figs / "confirmation_tradeoff.png")
        md += ["## Debounce trade-off: confirmation samples", "",
               "Requiring N consecutive samples trades latency against immunity to single-reading glitches "
               "(a +25 degC reading lasting less than one sample period).", "",
               "![confirmation trade-off](figures/confirmation_tradeoff.png)", ""]
    Path(a.results).write_text("\n".join(md), encoding="utf-8")
    print("wrote", a.results)


if __name__ == "__main__":
    main()
