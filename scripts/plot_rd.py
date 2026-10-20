#!/usr/bin/env python3
"""plot_rd.py — curva débito-distorção (SNR vs. kbps) do codec com perdas vs. MP3 e AAC.

Lê o CSV do scripts/benchmark_lossy.sh e desenha o SNR médio (nos ficheiros)
para cada débito pedido, uma linha por codec.

Uso: python3 scripts/plot_rd.py [-i results/benchmark_lossy.csv] [-o figura.png]
"""
import argparse
import csv
from collections import defaultdict

import matplotlib.pyplot as plt

# cor, marcador e nome de cada codec (cor nunca é a única pista: há rótulo e marcador)
CODECS = {
    "nosso": ("#2a78d6", "o", "Nosso (MDCT)"),
    "mp3": ("#eb6834", "s", "MP3 (LAME)"),
    "aac": ("#1baf7a", "^", "AAC (ffmpeg)"),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", default="results/benchmark_lossy.csv")
    ap.add_argument("-o", default="docs/relatorio/figuras/rd_lossy.png")
    a = ap.parse_args()

    snr = defaultdict(lambda: defaultdict(list))
    kb = defaultdict(lambda: defaultdict(list))
    with open(a.i) as f:
        for r in csv.DictReader(f):
            k = int(r["kbps_pedido"])
            snr[r["codec"]][k].append(float(r["snr_dB"]))
            kb[r["codec"]][k].append(float(r["kbps_real"]))

    fig, ax = plt.subplots(figsize=(6.4, 4.0), dpi=150)
    fins = []
    for codec, (cor, marc, nome) in CODECS.items():
        if codec not in snr:
            continue
        ks = sorted(snr[codec])
        x = [sum(kb[codec][k]) / len(kb[codec][k]) for k in ks]
        y = [sum(snr[codec][k]) / len(snr[codec][k]) for k in ks]
        ax.plot(x, y, color=cor, marker=marc, markersize=6, linewidth=2, label=nome)
        fins.append([y[-1], x[-1], y[-1], nome])

    # rótulos no fim de cada linha, afastados se ficarem a menos de 6 % da escala
    lo, hi = ax.get_ylim()
    sep = 0.06 * (hi - lo)
    fins.sort()
    for i in range(1, len(fins)):
        if fins[i][2] - fins[i - 1][2] < sep:
            fins[i][2] = fins[i - 1][2] + sep
    for _, xf, yl, nome in fins:
        ax.annotate(nome, (xf, yl), xytext=(6, 0), textcoords="offset points",
                    va="center", fontsize=9, color="#333333")

    ax.set_xlabel("Débito (kbit/s)")
    ax.set_ylabel("SNR médio (dB)")
    ax.set_xlim(left=0, right=ax.get_xlim()[1] * 1.22)   # espaço para os rótulos
    ax.grid(True, color="#e5e5e5", linewidth=0.8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.legend(frameon=False, loc="upper left")
    fig.tight_layout()
    fig.savefig(a.o)
    print("Figura em", a.o)


if __name__ == "__main__":
    main()
