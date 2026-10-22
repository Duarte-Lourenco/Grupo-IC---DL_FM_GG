#!/usr/bin/env python3
"""
varredura_bloco.py — mede taxa de compressao e tempo de codificacao em funcao do
tamanho do bloco, sobre o conjunto de treino. Produz as duas linhas da tabela do
relatorio ja em LaTeX.

Uso, a partir da raiz do repositorio:
    python3 varredura_bloco.py
    python3 varredura_bloco.py --data data/train --blocos 2048 4096 8192 16384 -n 3

Mede tempo de CPU e tempo de parede, melhor de n execucoes por ficheiro, e soma
ao longo dos 20. Verifica o governador antes de comecar, porque em powersave os
tempos nao sao comparaveis com o resto do relatorio.
"""
import argparse, os, resource, subprocess, sys, time

def cronometra(cmd):
    """Devolve (parede, cpu) de uma execucao, ou None se o comando falhar."""
    t0 = time.time()
    a = resource.getrusage(resource.RUSAGE_CHILDREN)
    r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    b = resource.getrusage(resource.RUSAGE_CHILDREN)
    if r.returncode != 0:
        return None
    cpu = (b.ru_utime - a.ru_utime) + (b.ru_stime - a.ru_stime)
    return time.time() - t0, cpu


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="data")
    ap.add_argument("--enc", default="build/wav_lossless_enc")
    ap.add_argument("--blocos", nargs="+", type=int,
                    default=[2048, 4096, 8192, 16384])
    ap.add_argument("-n", type=int, default=3, help="execucoes por ficheiro")
    a = ap.parse_args()

    if not os.path.isfile(a.enc):
        sys.exit(f"nao encontrei {a.enc} (compila primeiro)")

    gov = "?"
    try:
        gov = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor").read().strip()
    except OSError:
        pass
    print(f"governor: {gov}")
    if gov not in ("performance", "?"):
        print("  !! nao esta em performance, os tempos nao serao comparaveis")
        if input("  continuar? (s/N) ").strip().lower() != "s":
            return

    wavs = sorted(os.path.join(r, f)
                  for r, _, fs in os.walk(a.data) for f in fs
                  if f.lower().endswith(".wav"))
    if not wavs:
        sys.exit(f"nenhum .wav em {a.data}")
    orig = sum(os.path.getsize(w) for w in wavs)
    print(f"{len(wavs)} ficheiros, {orig/1048576:.2f} MiB no total, "
          f"melhor de {a.n} execucoes\n")

    res = {}
    for b in a.blocos:
        comp = 0
        parede = 0.0
        cpu = 0.0
        taxas = []
        for w in wavs:
            saida = f"/tmp/vb_{b}.icl"
            melhor = None
            for _ in range(a.n):
                t = cronometra([a.enc, w, saida, "-b", str(b)])
                if t is None:
                    sys.exit(f"o codificador falhou em {w} com bloco {b}")
                if melhor is None or t[0] < melhor[0]:
                    melhor = t
            tam = os.path.getsize(saida)
            comp += tam
            parede += melhor[0]
            cpu += melhor[1]
            taxas.append(os.path.getsize(w) / tam)
        res[b] = {"comp": comp, "total": orig / comp,
                  "media": sum(taxas) / len(taxas),
                  "parede": parede, "cpu": cpu}
        print(f"bloco {b:>6}  taxa_total {res[b]['total']:.4f}  "
              f"taxa_media {res[b]['media']:.4f}  "
              f"t_parede {parede:.3f} s  t_cpu {cpu:.3f} s")

    melhor_taxa = max(res, key=lambda b: res[b]["total"])
    print(f"\nmelhor taxa com bloco {melhor_taxa}")
    extremos = (max(r['total'] for r in res.values()) /
                min(r['total'] for r in res.values()) - 1) * 100
    print(f"diferenca entre extremos: {extremos:.2f} %")
    if 2048 in res and 4096 in res:
        d = (res[2048]["total"] / res[4096]["total"] - 1) * 100
        t = (res[2048]["parede"] / res[4096]["parede"] - 1) * 100
        print(f"2048 face a 4096: {d:+.2f} % de taxa, {t:+.1f} % de tempo")

    def pt(x, c=4):
        return f"{x:.{c}f}".replace(".", ",")
    print("\nLINHAS PARA A TABELA")
    print("% " + "-" * 56)
    print("Taxa de compressão (total) & " +
          " & ".join(pt(res[b]["total"]) for b in a.blocos) + r" \\")
    print("Tempo de codificação (s)   & " +
          " & ".join(pt(res[b]["parede"], 2) for b in a.blocos) + r" \\")
    print("% " + "-" * 56)
    print(f"(o tempo é o total dos {len(wavs)} ficheiros, tempo de parede)")


if __name__ == "__main__":
    main()
