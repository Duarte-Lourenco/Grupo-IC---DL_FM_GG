#!/usr/bin/env python3
"""Desenha histogramas produzidos pelo wav_hist.

Exemplos:
  # L, R, MID e SIDE de um ficheiro (chama o ./build/wav_hist)
  python3 scripts/plot_hist.py --wav data/sample01.wav
  python3 scripts/plot_hist.py --wav data/sample01.wav -k 6 --log -o hist.png

  # A partir de CSVs já gerados
  ./build/wav_hist data/sample01.wav mid -o mid.csv
  python3 scripts/plot_hist.py mid.csv

Sem -o, abre uma janela (no VSCode aparece no visualizador de gráficos).
"""
import argparse
import csv
import os
import subprocess
import sys
import tempfile

import matplotlib.pyplot as plt
import numpy as np

# Uma cor fixa por canal (a mesma em todos os gráficos do relatório)
COLORS = {"L": "#2a78d6", "R": "#eb6834", "MID": "#1baf7a", "SIDE": "#4a3aa7"}
DEFAULT_COLOR = "#2a78d6"

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WAV_HIST = os.path.join(REPO, "build", "wav_hist")


def read_csv(path):
    vals, counts = [], []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            vals.append(int(row["valor"]))
            counts.append(int(row["contagem"]))
    return np.array(vals), np.array(counts)


def run_wav_hist(wav, channel, k, outdir):
    out = os.path.join(outdir, f"{channel}.csv")
    cmd = [WAV_HIST, "-k", str(k), "-o", out, wav, channel]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.exit(f"Erro no wav_hist: {res.stderr.strip()}\n"
                 f"(compilaste? cmake -B build && cmake --build build)")
    return read_csv(out)


def plot_one(ax, vals, counts, bin_width, title, color, log):
    # Degraus contínuos: cada bin ocupa [valor, valor + 2^k)
    edges = np.append(vals, vals[-1] + bin_width)
    ax.stairs(counts, edges, fill=True, color=color, linewidth=0)
    ax.set_title(title, loc="left", fontsize=11)
    ax.set_xlabel("Valor da amostra")
    ax.set_ylabel("Contagem")
    if log:
        ax.set_yscale("log")
    ax.grid(axis="y", color="#e0dfdb", linewidth=0.6)
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("csv", nargs="*", help="CSVs gerados pelo wav_hist")
    p.add_argument("--wav", help="ficheiro WAV estéreo: desenha L, R, MID e SIDE")
    p.add_argument("-k", type=int, default=0, help="bins de 2^k valores (com --wav)")
    p.add_argument("--log", action="store_true", help="eixo y logarítmico")
    p.add_argument("-o", "--out", help="guardar em ficheiro (png/pdf) em vez de mostrar")
    a = p.parse_args()

    panels = []  # (vals, counts, bin_width, title, color)
    if a.wav:
        name = os.path.basename(a.wav)
        with tempfile.TemporaryDirectory() as tmp:
            for ch, label in (("0", "L"), ("1", "R"), ("mid", "MID"), ("side", "SIDE")):
                v, c = run_wav_hist(a.wav, ch, a.k, tmp)
                title = {"MID": "MID = (L+R)/2", "SIDE": "(L−R)/2"}.get(label, label)
                panels.append((v, c, 2 ** a.k, title, COLORS[label]))
        suptitle = f"{name} — bins de 2^{a.k} = {2 ** a.k} valor(es)"
    elif a.csv:
        for path in a.csv:
            v, c = read_csv(path)
            width = int(np.min(np.diff(v))) if len(v) > 1 else 1
            panels.append((v, c, width, os.path.basename(path), DEFAULT_COLOR))
        suptitle = None
    else:
        p.error("indica CSVs ou --wav")

    n = len(panels)
    cols = 2 if n > 1 else 1
    rows = (n + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(6 * cols, 3.6 * rows),
                             squeeze=False, sharex=bool(a.wav))
    for ax, (v, c, w, t, col) in zip(axes.flat, panels):
        plot_one(ax, v, c, w, t, col, a.log)
    for ax in axes.flat[n:]:
        ax.set_visible(False)
    if suptitle:
        fig.suptitle(suptitle, x=0.01, ha="left", fontsize=12)
    fig.tight_layout()

    if a.out:
        fig.savefig(a.out, dpi=150)
        print(f"Guardado em {a.out}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
