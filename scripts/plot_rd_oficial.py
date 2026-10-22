#!/usr/bin/env python3
"""plot_rd_oficial.py — curva debito-distorcao do codec com perdas, a partir dos
valores agregados do conjunto oficial de 20 ficheiros.

O scripts/plot_rd.py original le o CSV por ficheiro do benchmark_lossy.sh. Esse
CSV, no repositorio, e ainda do conjunto preliminar de 7 ficheiros. Este script
desenha a figura a partir das medias oficiais por debito, as mesmas que estao na
tabela do relatorio, o que garante que a figura e a tabela nao se contradizem.

Uso, a partir da raiz do repositorio:
    python3 scripts/plot_rd_oficial.py
    python3 scripts/plot_rd_oficial.py -i docs/resultados/rd_oficial.csv \
            -o docs/relatorio/figuras/rd_lossy.png
"""
import argparse
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

AZUL = "#2a78d6"
LARANJA = "#eb6834"
VERDE = "#1baf7a"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", default="docs/resultados/rd_oficial.csv")
    ap.add_argument("-o", default="docs/relatorio/figuras/rd_lossy.png")
    ap.add_argument("--coleccoes", action="store_true", default=True,
                    help="desenhar tambem as duas coleccoes em tracejado")
    ap.add_argument("--sem-coleccoes", dest="coleccoes", action="store_false")
    a = ap.parse_args()

    with open(a.i) as f:
        linhas = list(csv.DictReader(f))
    x = [float(r["kbps_pedido"]) for r in linhas]

    def col(nome):
        return [float(r[nome]) for r in linhas]

    fig, ax = plt.subplots(figsize=(6.4, 4.4), dpi=150)

    if a.coleccoes:
        ax.plot(x, col("nosso_sqam"), color=AZUL, linestyle=":", linewidth=1.4,
                marker="o", markersize=3.5, alpha=0.75)
        ax.plot(x, col("nosso_dsd100"), color=AZUL, linestyle="--", linewidth=1.4,
                marker="o", markersize=3.5, alpha=0.75)
        ax.annotate("SQAM", (x[-1], col("nosso_sqam")[-1]), xytext=(6, 0),
                    textcoords="offset points", va="center", fontsize=8, color=AZUL)
        ax.annotate("DSD100", (x[-1], col("nosso_dsd100")[-1]), xytext=(6, 0),
                    textcoords="offset points", va="center", fontsize=8, color=AZUL)

    series = [("nosso_media", AZUL, "o", "Nosso (MDCT)"),
              ("aac", VERDE, "^", "AAC (ffmpeg)"),
              ("mp3", LARANJA, "s", "MP3 (LAME)")]
    for nome, cor, marca, etiqueta in series:
        y = col(nome)
        ax.plot(x, y, color=cor, marker=marca, markersize=6, linewidth=2,
                label=etiqueta)
        ax.annotate(etiqueta.split(" ")[0], (x[-1], y[-1]), xytext=(6, 0),
                    textcoords="offset points", va="center", fontsize=9,
                    color=cor)

    ax.set_xlabel("Débito pedido (kbit/s)")
    ax.set_ylabel("SNR médio (dB)")
    ax.set_xticks(x)
    ax.set_xlim(x[0] - 10, x[-1] * 1.20)
    ax.set_ylim(bottom=0)
    ax.grid(True, color="#e5e5e5", linewidth=0.8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.legend(frameon=False, loc="upper left", fontsize=9)
    fig.tight_layout()

    destino = os.path.dirname(a.o)
    if destino:
        os.makedirs(destino, exist_ok=True)
    fig.savefig(a.o)
    print("Figura em", a.o)


if __name__ == "__main__":
    main()
