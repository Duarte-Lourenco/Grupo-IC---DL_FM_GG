#!/usr/bin/env python3
"""
bench_hist.py — compara o histograma em C++, em Python puro e em Python+numpy,
de forma JUSTA: as tres versoes fazem exactamente o mesmo trabalho (ler o WAV,
contar as ocorrencias de cada valor, escrever o CSV) e sao medidas da mesma
maneira (processo externo, tempo de CPU do filho, melhor de n execucoes).

Serve para substituir, no relatorio, a medicao antiga feita no sample01 do
conjunto preliminar.

Uso:
    python3 bench_hist.py <ficheiro.wav> [--bin build/wav_hist] [--canal 0] [-n 3]

Porque ha duas versoes em Python:
  * "Python puro" percorre as amostras uma a uma, que e o que um programa
    escrito sem bibliotecas faria;
  * "Python + numpy" delega a contagem numa rotina compilada.
Os dois numeros respondem a perguntas diferentes e vale a pena reportar ambos:
o primeiro mede a linguagem, o segundo mede a linguagem com biblioteca. Dar so
o primeiro e comparar C++ optimizado com Python mal escrito.
"""
import argparse, os, resource, subprocess, sys, tempfile, wave

HIST_PURO = r'''
import sys, wave, array
wav, canal, saida = sys.argv[1], int(sys.argv[2]), sys.argv[3]
with wave.open(wav, "rb") as w:
    nch = w.getnchannels(); n = w.getnframes()
    raw = w.readframes(n)
a = array.array("h"); a.frombytes(raw)
cont = {}
for i in range(canal, len(a), nch):
    v = a[i]
    cont[v] = cont.get(v, 0) + 1
with open(saida, "w") as f:
    f.write("valor,contagem\n")
    for v in sorted(cont):
        f.write("%d,%d\n" % (v, cont[v]))
'''

HIST_NUMPY = r'''
import sys, wave, numpy as np
wav, canal, saida = sys.argv[1], int(sys.argv[2]), sys.argv[3]
with wave.open(wav, "rb") as w:
    nch = w.getnchannels(); n = w.getnframes()
    raw = w.readframes(n)
x = np.frombuffer(raw, dtype="<i2")[canal::nch].astype(np.int32)
c = np.bincount(x + 32768, minlength=65536)
nz = np.nonzero(c)[0]
with open(saida, "w") as f:
    f.write("valor,contagem\n")
    for v in nz:
        f.write("%d,%d\n" % (v - 32768, c[v]))
'''


def cpu_filho():
    r = resource.getrusage(resource.RUSAGE_CHILDREN)
    return r.ru_utime + r.ru_stime


def cronometrar(cmd, n):
    """Melhor de n execucoes, tempo de CPU do processo filho."""
    melhor = None
    for _ in range(n):
        t0 = cpu_filho()
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        t = cpu_filho() - t0
        if r.returncode != 0:
            return None, r.stderr.decode(errors="replace")[-300:]
        melhor = t if melhor is None else min(melhor, t)
    return melhor, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--bin", default="build/wav_hist")
    ap.add_argument("--canal", type=int, default=0)
    ap.add_argument("-n", type=int, default=3)
    a = ap.parse_args()

    for p in (a.wav, a.bin):
        if not os.path.isfile(p):
            sys.exit(f"nao encontrei {p}")

    with wave.open(a.wav, "rb") as w:
        nch, sw, fs, nframes = (w.getnchannels(), w.getsampwidth(),
                                w.getframerate(), w.getnframes())
    if sw != 2:
        sys.exit(f"{a.wav}: este script so trata 16 bits (tem {sw*8})")
    print(f"ficheiro: {os.path.basename(a.wav)}")
    print(f"  {nframes} frames, {nch} canais, {fs} Hz, "
          f"{os.path.getsize(a.wav)/1048576:.2f} MiB, {nframes/fs:.1f} s\n")

    d = tempfile.mkdtemp(prefix="benchhist_")
    puro = os.path.join(d, "hist_puro.py")
    numpy_ = os.path.join(d, "hist_numpy.py")
    open(puro, "w").write(HIST_PURO)
    open(numpy_, "w").write(HIST_NUMPY)
    py = sys.executable

    casos = [
        ("C++ (wav_hist)", [a.bin, "-o", os.path.join(d, "c.csv"), a.wav, str(a.canal)]),
        ("Python puro",    [py, puro, a.wav, str(a.canal), os.path.join(d, "p.csv")]),
        ("Python + numpy", [py, numpy_, a.wav, str(a.canal), os.path.join(d, "n.csv")]),
    ]

    res = {}
    for nome, cmd in casos:
        t, err = cronometrar(cmd, a.n)
        if t is None:
            print(f"  aviso: {nome} falhou: {err}")
        res[nome] = t

    base = res["C++ (wav_hist)"]
    print(f"{'versao':<16}{'CPU (s)':>10}{'vs C++':>10}")
    print("-" * 36)
    for nome, _ in casos:
        t = res[nome]
        if t is None:
            print(f"{nome:<16}{'---':>10}{'---':>10}")
        else:
            print(f"{nome:<16}{t:>10.3f}{t/base:>9.1f}x")
    print(f"\n(melhor de {a.n} execucoes; tempo de CPU do processo, "
          f"inclui leitura do WAV e escrita do CSV nas tres versoes)")

    # verificacao: as tres versoes tem de produzir o mesmo histograma
    import csv as _csv
    def carregar(p):
        if not os.path.isfile(p): return None
        with open(p) as f:
            r = _csv.reader(f); next(r, None)
            return {int(a_): int(b_) for a_, b_ in r if a_}
    c, p_, n_ = carregar(os.path.join(d, "c.csv")), carregar(os.path.join(d, "p.csv")), carregar(os.path.join(d, "n.csv"))
    iguais = [x for x in (c, p_, n_) if x is not None]
    if len(iguais) > 1:
        ok = all(x == iguais[0] for x in iguais[1:])
        print("histogramas identicos entre versoes: " + ("SIM" if ok else "NAO (investigar)"))
    print(f"ficheiros intermedios em {d}")


if __name__ == "__main__":
    main()
