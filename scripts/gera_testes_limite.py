#!/usr/bin/env python3
"""Gera ficheiros WAV de teste para os casos-limite."""
import os
import struct
import wave

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "testes")
os.makedirs(OUT, exist_ok=True)
import glob as _g
_c = sorted(_g.glob(os.path.join(os.path.dirname(OUT), "data", "**", "*.wav"), recursive=True))
if not _c:
    raise SystemExit("nao encontrei nenhum .wav em data/")
SRC = min(_c, key=os.path.getsize)          # o menor, para os testes serem rapidos

w = wave.open(SRC, "rb")
P = w.getparams()
dados = w.readframes(min(P.nframes, 200000))
w.close()


def riff(path, chans, bits, rate, data, extra_chunks=b"", trunc=0):
    """Escreve um WAV a mao, com blocos extra opcionais e data truncado."""
    byte_rate = rate * chans * bits // 8
    fmt = struct.pack("<HHIIHH", 1, chans, rate, byte_rate, chans * bits // 8, bits)
    declarado = len(data)
    corpo = (b"WAVE"
             + b"fmt " + struct.pack("<I", len(fmt)) + fmt
             + extra_chunks
             + b"data" + struct.pack("<I", declarado) + data[:len(data) - trunc])
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(corpo)) + corpo)


# 16 bits estereo -> mono (fica com o canal esquerdo)
mono = bytearray()
for i in range(0, len(dados), 4):
    mono += dados[i:i + 2]
riff(f"{OUT}/mono16.wav", 1, 16, 44100, bytes(mono))

# 8 bits (sem sinal) estereo
oito = bytearray()
for i in range(0, len(dados), 2):
    v = struct.unpack_from("<h", dados, i)[0]
    oito.append((v >> 8) + 128)
riff(f"{OUT}/oito.wav", 2, 8, 44100, bytes(oito))

# 24 bits estereo (estende o 16 para 24)
vf = bytearray()
for i in range(0, len(dados), 2):
    v = struct.unpack_from("<h", dados, i)[0] * 256
    vf += struct.pack("<i", v)[0:3]
riff(f"{OUT}/vf24.wav", 2, 24, 44100, bytes(vf))

# 32 bits estereo
tv = bytearray()
for i in range(0, len(dados), 2):
    v = struct.unpack_from("<h", dados, i)[0] * 65536
    tv += struct.pack("<i", v)
riff(f"{OUT}/tv32.wav", 2, 32, 44100, bytes(tv))

# muito curto: 5 frames, e 1 frame
riff(f"{OUT}/curto5.wav", 2, 16, 44100, dados[:5 * 4])
riff(f"{OUT}/curto1.wav", 2, 16, 44100, dados[:1 * 4])
# 2 frames: menos do que as 3 do historico
riff(f"{OUT}/curto2.wav", 2, 16, 44100, dados[:2 * 4])

# com blocos de metadados antes e depois do data
lista = b"LIST" + struct.pack("<I", 12) + b"INFOINAMtest"
riff(f"{OUT}/meta.wav", 2, 16, 44100, dados[:40000], extra_chunks=lista)

# data truncado: o cabecalho declara mais bytes do que existem
riff(f"{OUT}/truncado.wav", 2, 16, 44100, dados[:40000], trunc=1000)

# numero impar de bytes no data (forca o byte de enchimento)
riff(f"{OUT}/impar.wav", 1, 8, 44100, bytes(oito[:4001]))

# casos do genero do EBU SQAM: tons, silencio, dual mono, saturacao
import math
def est(nome, L, R):
    riff(f"{OUT}/{nome}.wav", 2, 16, 44100,
         b"".join(struct.pack("<hh", a, b) for a, b in zip(L, R)))

N = 44100
tom = [int(20000 * math.sin(2 * math.pi * 997 * i / 44100)) for i in range(N)]
est("silencio", [0] * N, [0] * N)
est("dualmono", tom, tom)
est("umcanal", tom, [0] * N)
est("tom", tom, [int(20000 * math.sin(2 * math.pi * 440 * i / 44100)) for i in range(N)])
est("quadrada", [32767 if (i // 50) % 2 else -32768 for i in range(N)],
    [32767 if (i // 50) % 2 else -32768 for i in range(N)])

for f in sorted(os.listdir(OUT)):
    print("%-14s %8d bytes" % (f, os.path.getsize(os.path.join(OUT, f))))