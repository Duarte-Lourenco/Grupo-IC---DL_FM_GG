#!/usr/bin/env python3
"""Mede tudo o que o relatório precisa, numa só máquina e de uma só vez.

Compara a versão actual do codec (a que está compilada em build/) com a
versão do branch main, e com o estado da arte. Mede também um ficheiro
dez vezes maior, para mostrar que a memória não cresce com o tamanho.

    python3 scripts/medir_tudo.py [pasta]

A pasta por omissão é data/. Os ficheiros .wav são procurados
recursivamente e agrupados pela subpasta onde estão, de modo que
data/train/dsd100/ e data/train/sqam/ aparecem em linhas separadas,
além do total.

No fim imprime um bloco de texto pronto a copiar.
"""
import glob
import os
import re
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(RAIZ)
TMP = "/tmp/ic_medir"
ANTIGO = "/tmp/ic_antigo"          # worktree do main, para a versão antiga
os.makedirs(TMP, exist_ok=True)

NOVO_ENC = os.path.join(RAIZ, "build", "wav_lossless_enc")
NOVO_DEC = os.path.join(RAIZ, "build", "wav_lossless_dec")
C = os.path.join(TMP, "c.bin")
D = os.path.join(TMP, "d.wav")


def erro(msg):
    print("ERRO: " + msg)
    sys.exit(1)


def medir(cmd, reps=3):
    """Corre o comando reps vezes; devolve (melhor tempo em s, pico em KiB)."""
    melhor = None
    for _ in range(reps):
        p = subprocess.run(["/usr/bin/time", "-v"] + cmd, capture_output=True, text=True)
        if p.returncode != 0:
            return None, None
        m = re.search(r"Elapsed \(wall clock\).*?([0-9]+:[0-9.]+)\s*$", p.stderr, re.M)
        q = m.group(1).split(":")
        s = float(q[-1]) + (60 * float(q[-2]) if len(q) > 1 else 0)
        k = int(re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", p.stderr).group(1))
        melhor = (s, k) if melhor is None or s < melhor[0] else (melhor[0], max(melhor[1], k))
    return melhor


def igual(a, b):
    return subprocess.run(["cmp", "-s", a, b]).returncode == 0


def info_maquina():
    cpu = "?"
    try:
        for l in open("/proc/cpuinfo"):
            if l.startswith("model name"):
                cpu = l.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    mem = "?"
    try:
        for l in open("/proc/meminfo"):
            if l.startswith("MemTotal"):
                mem = "%.1f GiB" % (int(l.split()[1]) / 1048576)
                break
    except OSError:
        pass
    def v(cmd):
        try:
            return subprocess.run(cmd, capture_output=True, text=True).stdout.split("\n")[0].strip()
        except OSError:
            return "(nao instalado)"
    return cpu, mem, v(["g++", "--version"]), v(["flac", "--version"]), \
        v(["uname", "-sr"]), v(["cmake", "--version"])


# ---------------------------------------------------------------- preparacao --
if not (os.path.isfile(NOVO_ENC) and os.path.isfile(NOVO_DEC)):
    erro("build/wav_lossless_enc nao existe. Compila primeiro:\n"
         "  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j")

PASTA = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, "data")
if not os.path.isabs(PASTA):
    PASTA = os.path.join(RAIZ, PASTA)
ficheiros = sorted(glob.glob(os.path.join(PASTA, "**", "*.wav"), recursive=True))
if not ficheiros:
    erro("nao encontrei nenhum .wav em " + PASTA)


def colecao(w):
    """Nome da subpasta onde o ficheiro esta, relativo a PASTA."""
    rel = os.path.relpath(os.path.dirname(w), PASTA)
    return "(raiz)" if rel == "." else rel.replace(os.sep, "/")


grupos = sorted({colecao(w) for w in ficheiros})
print("%d ficheiros em %s" % (len(ficheiros), PASTA))
for g in grupos:
    print("   %-24s %d ficheiros" % (g, sum(1 for w in ficheiros if colecao(w) == g)))

# versao antiga: worktree do main numa pasta a parte, para nao mexer no branch
ant_enc = os.path.join(ANTIGO, "build", "wav_lossless_enc")
ant_dec = os.path.join(ANTIGO, "build", "wav_lossless_dec")
tem_antigo = False
if os.path.isfile(ant_enc) and os.path.isfile(ant_dec):
    tem_antigo = True
    print("versao antiga: ja compilada em %s" % ANTIGO)
else:
    print("A preparar a versao antiga (branch main) em %s ..." % ANTIGO)
    if not os.path.isdir(ANTIGO):
        r = subprocess.run(["git", "worktree", "add", ANTIGO, "main"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print("  nao consegui criar o worktree: " + r.stderr.strip()[:200])
    if os.path.isdir(ANTIGO):
        if not os.path.exists(os.path.join(ANTIGO, "data")):
            os.symlink(os.path.join(RAIZ, "data"), os.path.join(ANTIGO, "data"))
        subprocess.run(["cmake", "-B", os.path.join(ANTIGO, "build"),
                        "-DCMAKE_BUILD_TYPE=Release", "-S", ANTIGO], capture_output=True)
        subprocess.run(["cmake", "--build", os.path.join(ANTIGO, "build"), "-j"],
                       capture_output=True)
        tem_antigo = os.path.isfile(ant_enc) and os.path.isfile(ant_dec)
    if not tem_antigo:
        print("  Sem a versao antiga: a comparacao sera so da versao nova.")

# ------------------------------------------------------------------ medicoes --
def mede_codec(enc, dec, nome):
    """Devolve (linhas por ficheiro, agregados)."""
    linhas = []
    for w in ficheiros:
        te, me = medir([enc, w, C])
        if te is None:
            print("  %s falhou em %s" % (nome, os.path.basename(w)))
            return None, None
        comp = os.path.getsize(C)
        td, md = medir([dec, C, D])
        if td is None:
            return None, None
        linhas.append(dict(f=os.path.basename(w)[:-4], grupo=colecao(w),
                           orig=os.path.getsize(w), comp=comp,
                           te=te, td=td, me=me, md=md, ex=igual(w, D)))
    n = len(linhas)
    ag = dict(
        taxa_media=sum(l["orig"] / l["comp"] for l in linhas) / n,
        taxa_total=sum(l["orig"] for l in linhas) / sum(l["comp"] for l in linhas),
        te=sum(l["te"] for l in linhas) / n, td=sum(l["td"] for l in linhas) / n,
        me=max(l["me"] for l in linhas), md=max(l["md"] for l in linhas),
        ex=all(l["ex"] for l in linhas))
    return linhas, ag


def mede_externo(nome, enc, dec):
    o = c = 0
    te = td = 0.0
    me = md = 0
    ex = True
    for w in ficheiros:
        t1, k1 = medir(enc(w))
        if t1 is None:
            return None
        o += os.path.getsize(w)
        c += os.path.getsize(TMP + "/x")
        t2, k2 = medir(dec())
        if t2 is None:
            return None
        te += t1; td += t2; me = max(me, k1); md = max(md, k2)
        if not igual(w, D):
            ex = False
    n = len(ficheiros)
    return dict(nome=nome, taxa_media=0, taxa_total=o / c, te=te / n, td=td / n,
                me=me, md=md, ex=ex, o=o, c=c)


print("\nA medir a versao NOVA (%d ficheiros, 3 corridas cada) ..." % len(ficheiros))
lin_novo, ag_novo = mede_codec(NOVO_ENC, NOVO_DEC, "nova")
if ag_novo is None:
    erro("a versao nova falhou")

ag_antigo = None
if tem_antigo:
    print("A medir a versao ANTIGA ...")
    _, ag_antigo = mede_codec(ant_enc, ant_dec, "antiga")

externos = []
print("A medir o estado da arte ...")
for nome, encf, decf in [
    ("flac -8",  lambda w: ["flac", "-8", "-f", "-s", "-o", TMP + "/x", w],
                 lambda: ["flac", "-d", "-f", "-s", "-o", D, TMP + "/x"]),
    ("flac -5",  lambda w: ["flac", "-5", "-f", "-s", "-o", TMP + "/x", w],
                 lambda: ["flac", "-d", "-f", "-s", "-o", D, TMP + "/x"]),
]:
    r = mede_externo(nome, encf, decf)
    if r:
        externos.append(r)

# xz e zstd escrevem para stdout, por isso vao por shell
def mede_shell(nome, cenc, cdec):
    o = c = 0
    te = td = 0.0
    me = md = 0
    for w in ficheiros:
        t1, k1 = medir(["bash", "-c", cenc % (w, TMP + "/x")])
        if t1 is None:
            return None
        o += os.path.getsize(w); c += os.path.getsize(TMP + "/x")
        t2, k2 = medir(["bash", "-c", cdec % (TMP + "/x", D)])
        if t2 is None:
            return None
        te += t1; td += t2; me = max(me, k1); md = max(md, k2)
    n = len(ficheiros)
    return dict(nome=nome, taxa_total=o / c, te=te / n, td=td / n, me=me, md=md, ex=True)


for nome, a, b in [("xz -9", "xz -9 -T1 -c '%s' > '%s'", "xz -d -c '%s' > '%s'"),
                   ("zstd -19", "zstd -19 -q -T1 -c '%s' > '%s'", "zstd -d -q -c '%s' > '%s'")]:
    r = mede_shell(nome, a, b)
    if r:
        externos.append(r)

# ------------------------------------------------- ficheiro dez vezes maior --
print("A preparar o ficheiro grande ...")
LONGO = os.path.join(TMP, "longo.wav")
grande = {}
try:
    import wave
    menor = min(ficheiros, key=os.path.getsize)
    repete = max(2, min(20, int(45 * 1048576 / max(1, os.path.getsize(menor)))))
    w = wave.open(menor, "rb")
    P = w.getparams()
    dados = w.readframes(P.nframes)
    w.close()
    o = wave.open(LONGO, "wb")
    o.setparams(P)
    for _ in range(repete):
        o.writeframes(dados)
    o.close()
    print("  (%s repetido %dx)" % (os.path.basename(menor), repete))
    print("A medir o ficheiro grande (%.1f MiB) ..." % (os.path.getsize(LONGO) / 1048576))
    for tag, e, d in ([("nova", NOVO_ENC, NOVO_DEC)] +
                      ([("antiga", ant_enc, ant_dec)] if tem_antigo else [])):
        te, me = medir([e, LONGO, TMP + "/L.bin"], reps=1)
        td, md = medir([d, TMP + "/L.bin", TMP + "/L.wav"], reps=1)
        if te is not None:
            grande[tag] = dict(te=te, me=me, td=td, md=md,
                               ex=igual(LONGO, TMP + "/L.wav"),
                               taxa=os.path.getsize(LONGO) / os.path.getsize(TMP + "/L.bin"))
    t, k = medir(["flac", "-8", "-f", "-s", "-o", TMP + "/L.flac", LONGO], reps=1)
    if t is not None:
        grande["flac -8"] = dict(te=t, me=k, td=0, md=0, ex=True,
                                 taxa=os.path.getsize(LONGO) / os.path.getsize(TMP + "/L.flac"))
except Exception as e:                                  # noqa: BLE001
    print("  nao consegui fazer o ficheiro grande: %s" % e)

# ------------------------------------------------------------------- relatorio -
cpu, mem, gpp, vflac, uname, vcmake = info_maquina()
L = []
A = L.append
A("===== MEDICOES PARA O RELATORIO =====")
A("")
A("[MAQUINA]")
A("CPU:      %s" % cpu)
A("Memoria:  %s" % mem)
A("Sistema:  %s" % uname)
A("g++:      %s" % gpp)
A("cmake:    %s" % vcmake)
A("flac:     %s" % vflac)
A("Nucleos:  %d" % (os.cpu_count() or 0))
A("")
A("[POR COLECCAO - VERSAO NOVA]")
A("%-24s %3s %9s %9s %7s %7s" % ("coleccao", "n", "taxa_med", "taxa_tot", "t_enc", "t_dec"))
for g in grupos:
    sel = [l for l in lin_novo if l["grupo"] == g]
    if not sel:
        continue
    A("%-24s %3d %9.4f %9.4f %7.3f %7.3f"
      % (g, len(sel), sum(l["orig"] / l["comp"] for l in sel) / len(sel),
         sum(l["orig"] for l in sel) / sum(l["comp"] for l in sel),
         sum(l["te"] for l in sel) / len(sel), sum(l["td"] for l in sel) / len(sel)))
A("")
A("[POR FICHEIRO - VERSAO NOVA]")
A("%-28s %11s %11s %7s %7s %7s %10s %10s %6s"
  % ("ficheiro", "bytes_orig", "bytes_comp", "taxa", "t_enc", "t_dec",
     "mem_enc_k", "mem_dec_k", "exato"))
for l in lin_novo:
    A("%-28s %11d %11d %7.4f %7.3f %7.3f %10d %10d  %s"
      % (l["f"][:28], l["orig"], l["comp"], l["orig"] / l["comp"], l["te"], l["td"],
         l["me"], l["md"], "sim" if l["ex"] else "NAO"))
A("")
A("[RESUMO - %d FICHEIROS]" % len(ficheiros))
A("%-12s %9s %9s %7s %7s %10s %10s %6s"
  % ("versao", "taxa_med", "taxa_tot", "t_enc", "t_dec", "mem_enc_k", "mem_dec_k", "exato"))
if ag_antigo:
    A("%-12s %9.4f %9.4f %7.3f %7.3f %10d %10d %6s"
      % ("antiga", ag_antigo["taxa_media"], ag_antigo["taxa_total"], ag_antigo["te"],
         ag_antigo["td"], ag_antigo["me"], ag_antigo["md"],
         "sim" if ag_antigo["ex"] else "NAO"))
A("%-12s %9.4f %9.4f %7.3f %7.3f %10d %10d %6s"
  % ("NOVA", ag_novo["taxa_media"], ag_novo["taxa_total"], ag_novo["te"], ag_novo["td"],
     ag_novo["me"], ag_novo["md"], "sim" if ag_novo["ex"] else "NAO"))
for r in externos:
    A("%-12s %9s %9.4f %7.3f %7.3f %10d %10d %6s"
      % (r["nome"], "-", r["taxa_total"], r["te"], r["td"], r["me"], r["md"],
         "sim" if r["ex"] else "NAO"))
A("")
if grande:
    A("[FICHEIRO GRANDE - %.1f MiB]" % (os.path.getsize(LONGO) / 1048576))
    A("%-10s %8s %7s %10s %10s %6s" % ("versao", "taxa", "t_enc", "mem_enc_k", "mem_dec_k", "exato"))
    for tag in ["antiga", "nova", "flac -8"]:
        if tag in grande:
            g = grande[tag]
            A("%-10s %8.4f %7.2f %10d %10d %6s"
              % (tag, g["taxa"], g["te"], g["me"], g["md"], "sim" if g["ex"] else "NAO"))
    A("")
A("===== FIM =====")

txt = "\n".join(L)
print("\n" + txt)
destino = os.path.join(RAIZ, "docs", "resultados")
os.makedirs(destino, exist_ok=True)
with open(os.path.join(destino, "medicoes.txt"), "w") as f:
    f.write(txt + "\n")
print("\n(guardado tambem em docs/resultados/medicoes.txt)")