#!/usr/bin/env python3
"""
verifica_numpy.py — recalculo independente, em numpy, dos valores que o codigo
em C++ produz. Serve de segunda implementacao: nao chama nenhum programa do
projecto para calcular, apenas para comparar.

Verifica tres coisas:

  1. ENTROPIAS E DESVIOS PADRAO (contra o \\prog{wav_hist})
     Le o WAV com numpy, constroi os histogramas de L, R, MID e (L-R)/2 e
     calcula a entropia de ordem zero e o desvio padrao de cada um. Compara com
     o CSV que o wav_hist produz.

  2. METRICAS DE ERRO (contra o \\prog{wav_cmp})
     Quantiza um ficheiro com o wav_quant e recalcula MSE, L-infinito e SNR
     sobre os dois WAV, comparando com o que o wav_cmp imprime.

  3. RECONSTRUCAO MID/SIDE
     Confirma, em todas as amostras, que
     L + R = 2*MID + (SIDE & 1)  com MID = floor((L+R)/2) e SIDE = L-R.

Uso, a partir da raiz do repositorio:
    python3 verifica_numpy.py                         # um ficheiro por coleccao
    python3 verifica_numpy.py --todos                 # os 20
    python3 verifica_numpy.py --wav data/train/dsd100/052_mix.wav

Sai com codigo 1 se alguma comparacao falhar, para poder ser usado em CI.
"""
import argparse, csv, os, subprocess, sys, tempfile, wave

try:
    import numpy as np
except ImportError:
    sys.exit("falta o numpy:  sudo apt install -y python3-numpy")

TOL_H = 1e-4      # bits/amostra
TOL_S = 1e-3      # desvio padrao, em unidades de amplitude
TOL_REL = 1e-6    # tolerancia relativa nas metricas de erro
falhas = 0


def ok(cond, texto, obtido="", esperado=""):
    global falhas
    if cond:
        print(f"   OK    {texto}")
    else:
        falhas += 1
        print(f"   FALHA {texto}   numpy={obtido}  C++={esperado}")


def ler_wav(path):
    """Devolve (amostras int32 por canal, taxa). Sem libsndfile e sem wav_io."""
    with wave.open(path, "rb") as w:
        nch, sw, fs, n = (w.getnchannels(), w.getsampwidth(),
                          w.getframerate(), w.getnframes())
        raw = w.readframes(n)
    if sw == 1:
        x = np.frombuffer(raw, dtype=np.uint8).astype(np.int32) - 128
    elif sw == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.int32)
    elif sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        x = np.where(v & 0x800000, v - 0x1000000, v)
    elif sw == 4:
        x = np.frombuffer(raw, dtype="<i4").astype(np.int32)
    else:
        raise RuntimeError(f"{path}: {sw*8} bits nao suportado")
    return x.reshape(-1, nch), fs


def entropia(v):
    """Entropia de ordem zero, em bits por amostra, a partir das contagens."""
    _, c = np.unique(v, return_counts=True)
    p = c / c.sum()
    return float(-(p * np.log2(p)).sum())


def canais(x):
    """L, R, MID = floor((L+R)/2) e a diferenca media (L-R)/2, com a mesma
    convencao de divisao inteira do C++ (arredondamento para baixo)."""
    L = x[:, 0].astype(np.int64)
    R = x[:, 1].astype(np.int64) if x.shape[1] > 1 else L
    return {"L": L, "R": R, "MID": (L + R) >> 1, "HALF": (L - R) >> 1}


def le_csv(path):
    with open(path) as f:
        r = csv.reader(f)
        next(r, None)
        d = {int(a): int(b) for a, b in r if a}
    v = np.fromiter(d.keys(), dtype=np.int64)
    c = np.fromiter(d.values(), dtype=np.int64)
    p = c / c.sum()
    h = float(-(p * np.log2(p)).sum())
    m = float((v * p).sum())
    s = float(np.sqrt((p * (v - m) ** 2).sum()))
    return h, s


def bloco1(wav, binario, tmp):
    print(f"\n[1] entropias e desvios padrao -- {os.path.basename(wav)}")
    x, _ = ler_wav(wav)
    ch = canais(x)
    arg = {"L": "0", "R": "1", "MID": "mid", "HALF": "side"}
    for nome, v in ch.items():
        saida = os.path.join(tmp, f"v_{nome}.csv")
        r = subprocess.run([binario, "-o", saida, wav, arg[nome]],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if r.returncode != 0:
            ok(False, f"{nome}: o wav_hist falhou",
               "", r.stderr.decode(errors="replace")[-120:])
            continue
        h_c, s_c = le_csv(saida)
        h_n, s_n = entropia(v), float(v.std())
        ok(abs(h_n - h_c) < TOL_H, f"{nome}: entropia {h_n:.4f} bits/amostra",
           f"{h_n:.6f}", f"{h_c:.6f}")
        ok(abs(s_n - s_c) < TOL_S, f"{nome}: desvio padrao {s_n:.2f}",
           f"{s_n:.4f}", f"{s_c:.4f}")


def bloco2(wav, build, tmp):
    print(f"\n[2] metricas de erro -- {os.path.basename(wav)}, quantizado a 8 bits")
    q = os.path.join(tmp, "q.wav")
    r = subprocess.run([f"{build}/wav_quant", "-b", "8", wav, q],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if r.returncode != 0:
        ok(False, "o wav_quant falhou", "", r.stderr.decode(errors="replace")[-120:])
        return
    a, _ = ler_wav(wav)
    b, _ = ler_wav(q)
    n = min(len(a), len(b))
    e = a[:n, 0].astype(np.float64) - b[:n, 0].astype(np.float64)
    s = a[:n, 0].astype(np.float64)
    mse_n = float((e ** 2).mean())
    linf_n = int(np.abs(e).max())
    snr_n = float(10 * np.log10((s ** 2).sum() / (e ** 2).sum()))

    out = subprocess.run([f"{build}/wav_cmp", wav, q],
                         capture_output=True, text=True).stdout
    linha = next((l for l in out.splitlines() if l.strip().startswith("L ")), "")
    import re
    g = re.search(r"MSE\s*=\s*([0-9.]+).*Linf\s*=\s*([0-9]+).*SNR\s*=\s*([0-9.-]+)", linha)
    if not g:
        ok(False, "nao consegui ler a saida do wav_cmp", "", linha[:80])
        return
    mse_c, linf_c, snr_c = float(g.group(1)), int(g.group(2)), float(g.group(3))
    ok(abs(mse_n - mse_c) <= TOL_REL * max(1.0, mse_c), f"MSE {mse_n:.4f}",
       f"{mse_n:.6f}", f"{mse_c:.6f}")
    ok(linf_n == linf_c, f"L-infinito {linf_n}", linf_n, linf_c)
    ok(abs(snr_n - snr_c) < 1e-3, f"SNR {snr_n:.4f} dB", f"{snr_n:.6f}", f"{snr_c:.6f}")


def bloco3(wav):
    print(f"\n[3] reconstrucao MID/SIDE -- {os.path.basename(wav)}")
    x, _ = ler_wav(wav)
    if x.shape[1] < 2:
        print("   (mono: nao se aplica)")
        return
    L = x[:, 0].astype(np.int64)
    R = x[:, 1].astype(np.int64)
    mid = (L + R) >> 1
    side = L - R
    soma = 2 * mid + (side & 1)
    Lr = (soma + side) >> 1
    Rr = (soma - side) >> 1
    ok(bool(np.array_equal(Lr, L) and np.array_equal(Rr, R)),
       f"L e R reconstruidos exatamente em {len(L)} amostras")
    ok(bool(np.array_equal((L + R) & 1, side & 1)),
       "L+R e L-R tem a mesma paridade em todas as amostras")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="data")
    ap.add_argument("--build", default="build")
    ap.add_argument("--wav", default=None, help="verificar apenas este ficheiro")
    ap.add_argument("--todos", action="store_true",
                    help="verificar os 20 (demora bastante mais)")
    a = ap.parse_args()

    if not os.path.isdir(a.build):
        sys.exit(f"nao encontrei {a.build} (compila primeiro)")

    if a.wav:
        wavs = [a.wav]
    else:
        todos = sorted(os.path.join(r, f)
                       for r, _, fs in os.walk(a.data) for f in fs
                       if f.lower().endswith(".wav"))
        if not todos:
            sys.exit(f"nenhum .wav em {a.data}")
        if a.todos:
            wavs = todos
        else:
            dsd = [w for w in todos if "sqam" not in os.path.basename(w)]
            sqa = [w for w in todos if "sqam" in os.path.basename(w)]
            wavs = ([dsd[0]] if dsd else []) + ([sqa[0]] if sqa else [])

    print("Recalculo independente em numpy " + np.__version__)
    print("Nenhum valor abaixo vem do codigo em C++; so as comparacoes.")
    tmp = tempfile.mkdtemp(prefix="verif_")
    for w in wavs:
        bloco1(w, os.path.join(a.build, "wav_hist"), tmp)
        bloco2(w, a.build, tmp)
        bloco3(w)

    print("\n" + "=" * 60)
    if falhas:
        print(f"{falhas} COMPARACOES FALHARAM")
        sys.exit(1)
    print("Todas as comparacoes coincidem. As duas implementacoes concordam.")


if __name__ == "__main__":
    main()
