#!/usr/bin/env python3
"""
analise_parte1.py — refaz a analise exploratoria da Parte I nos 20 ficheiros
oficiais: entropia de ordem zero e desvio padrao dos canais L, R, MID e SIDE.

Corre o ./build/wav_hist uma vez por canal e por ficheiro, le os CSV e calcula
tudo a partir das contagens. Imprime:
  * uma linha por ficheiro;
  * as medias por coleccao;
  * quantos ficheiros ficam mais baratos em MID/SIDE do que em L/R, que e o
    numero que justifica decidir o modo de canais bloco a bloco;
  * as linhas prontas a colar na tabela do relatorio.

Uso, a partir da raiz do repositorio:
    python3 analise_parte1.py
    python3 analise_parte1.py --data data/train --bin build/wav_hist
    python3 analise_parte1.py --figura data/train/dsd100/052_mix.wav
"""
import argparse, csv, math, os, subprocess, sys, tempfile

CANAIS = ["0", "1", "mid", "side"]
NOMES = {"0": "L", "1": "R", "mid": "MID", "side": "SIDE"}


def hist(binario, wav, canal, tmp):
    """Corre o wav_hist e devolve {valor: contagem}."""
    saida = os.path.join(tmp, f"h_{canal}.csv")
    r = subprocess.run([binario, "-o", saida, wav, canal],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if r.returncode != 0:
        raise RuntimeError(f"{wav} canal {canal}: "
                           f"{r.stderr.decode(errors='replace')[-200:]}")
    with open(saida) as f:
        rd = csv.reader(f)
        next(rd, None)
        return {int(a): int(b) for a, b in rd if a}


def entropia_e_sigma(cont):
    """Entropia de ordem zero (bits/amostra) e desvio padrao, das contagens."""
    n = sum(cont.values())
    if n == 0:
        return 0.0, 0.0
    h = 0.0
    media = 0.0
    for v, c in cont.items():
        p = c / n
        h -= p * math.log2(p)
        media += v * p
    var = sum(c / n * (v - media) ** 2 for v, c in cont.items())
    return h, math.sqrt(var)


def coleccao(caminho):
    base = os.path.basename(caminho)
    return "SQAM" if base.startswith("sqam") else "DSD100"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="data", help="pasta onde procurar os .wav")
    ap.add_argument("--bin", default="build/wav_hist")
    ap.add_argument("--figura", default=None,
                    help="gera tambem a figura deste ficheiro (chama scripts/plot_hist.py)")
    ap.add_argument("--k", type=int, default=8, help="bins de 2^k para a figura")
    a = ap.parse_args()

    if not os.path.isfile(a.bin):
        sys.exit(f"nao encontrei {a.bin} (compila primeiro)")

    wavs = []
    for raiz, _, fichs in os.walk(a.data):
        for f in fichs:
            if f.lower().endswith(".wav"):
                wavs.append(os.path.join(raiz, f))
    wavs.sort(key=lambda p: (coleccao(p), os.path.basename(p)))
    if not wavs:
        sys.exit(f"nenhum .wav em {a.data}")
    print(f"{len(wavs)} ficheiros encontrados em {a.data}\n")

    linhas = []
    tmp = tempfile.mkdtemp(prefix="parte1_")
    for w in wavs:
        try:
            d = {c: entropia_e_sigma(hist(a.bin, w, c, tmp)) for c in CANAIS}
        except RuntimeError as e:
            print(f"  ERRO: {e}")
            continue
        linhas.append({
            "ficheiro": os.path.splitext(os.path.basename(w))[0],
            "col": coleccao(w),
            "HL": d["0"][0], "HR": d["1"][0],
            "HM": d["mid"][0], "HS": d["side"][0],
            "sL": d["0"][1], "sR": d["1"][1],
            "sM": d["mid"][1], "sS": d["side"][1],
        })
        print(f"  {linhas[-1]['ficheiro']:<12} feito")

    if not linhas:
        sys.exit("nada foi calculado")

    cab = (f"\n{'ficheiro':<12}{'H_L':>7}{'H_R':>7}{'H_MID':>8}{'H_SIDE':>8}"
           f"{'sigma_L':>10}{'sigma_S':>10}{'16/H_L':>8}  MS<LR")
    print(cab); print("-" * len(cab))
    melhores = 0
    for L in linhas:
        ms = (L["HM"] + L["HS"]) < (L["HL"] + L["HR"])
        melhores += ms
        print(f"{L['ficheiro']:<12}{L['HL']:>7.2f}{L['HR']:>7.2f}{L['HM']:>8.2f}"
              f"{L['HS']:>8.2f}{L['sL']:>10.0f}{L['sS']:>10.0f}"
              f"{16/L['HL']:>8.2f}  {'sim' if ms else 'NAO'}")

    print("\nMEDIAS POR COLECCAO")
    print(f"{'coleccao':<12}{'H_L':>7}{'H_R':>7}{'H_MID':>8}{'H_SIDE':>8}"
          f"{'sigma_L':>10}{'sigma_S':>10}{'16/H_L':>8}")
    med = {}
    for col in ("DSD100", "SQAM"):
        g = [L for L in linhas if L["col"] == col]
        if not g:
            continue
        m = {k: sum(L[k] for L in g) / len(g) for k in
             ("HL", "HR", "HM", "HS", "sL", "sS")}
        med[col] = m
        print(f"{col:<12}{m['HL']:>7.2f}{m['HR']:>7.2f}{m['HM']:>8.2f}"
              f"{m['HS']:>8.2f}{m['sL']:>10.0f}{m['sS']:>10.0f}"
              f"{16/m['HL']:>8.2f}")

    print(f"\nMID/SIDE mais barato que L/R em {melhores} dos {len(linhas)} ficheiros"
          f"  ({len(linhas)-melhores} em que NAO compensa)")
    print("  -> e este numero que justifica decidir o modo de canais bloco a bloco")

    hs = [L["HL"] for L in linhas]
    print(f"\nH_L varia entre {min(hs):.2f} e {max(hs):.2f} bits/amostra")
    print(f"taxa que a entropia de ordem zero permitiria: "
          f"{16/max(hs):.2f} a {16/min(hs):.2f}")

    # --- linhas prontas a colar no LaTeX (extremos de cada coleccao + medias)
    print("\n\nLINHAS PARA A TABELA (copiar para o .tex)")
    print("% " + "-" * 60)
    sel = []
    for col in ("DSD100", "SQAM"):
        g = sorted([L for L in linhas if L["col"] == col], key=lambda L: L["HL"])
        if g:
            sel += [g[0], g[-1]]
    def pt(x, casas=2):
        return f"{x:.{casas}f}".replace(".", ",")
    for L in sel:
        nome = L["ficheiro"].replace("_", chr(92) + "_")
        print(f"\\texttt{{{nome}}} & {pt(L['HL'])} & {pt(L['HR'])} & {pt(L['HM'])} & "
              f"{pt(L['HS'])} & {pt(L['sL'],0)} & {pt(L['sS'],0)} & {pt(16/L['HL'])} \\\\")
    print("\\midrule")
    for col, m in med.items():
        nome = "Média DSD100" if col == "DSD100" else "Média SQAM"
        print(f"{nome} & {pt(m['HL'])} & {pt(m['HR'])} & {pt(m['HM'])} & "
              f"{pt(m['HS'])} & {pt(m['sL'],0)} & {pt(m['sS'],0)} & {pt(16/m['HL'])} \\\\")
    print("% " + "-" * 60)

    if a.figura:
        destino = "docs/relatorio/figuras/hist_parte1.png"
        os.makedirs(os.path.dirname(destino), exist_ok=True)
        cmd = [sys.executable, "scripts/plot_hist.py", "--wav", a.figura,
               "-k", str(a.k), "--log", "-o", destino]
        print(f"\na gerar a figura: {' '.join(cmd)}")
        r = subprocess.run(cmd)
        if r.returncode == 0:
            print(f"escrito: {destino}")
        else:
            print("a figura falhou; corre o comando a mao para veres o erro")


if __name__ == "__main__":
    main()
