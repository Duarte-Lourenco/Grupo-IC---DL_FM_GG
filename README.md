# Information and Coding 2026/27 — Trabalho Prático n.º 1

Ferramentas de análise de áudio, um codec sem perdas e um codec com perdas
baseado na MDCT, em C++20.

| Autor | N.º mec. | Contribuição |
|---|---|---|
| Duarte Lourenço | 114421 | Base comum, `wav_hist`, BitStream, Golomb/Rice, codec sem perdas, comparação com FLAC/zstd/xz |
| Francisco Matos | 113726 | `wav_cmp`, `wav_quant`, `wav_effects`, `metrics.hpp`, MDCT, codec com perdas, comparação com MP3/AAC |

O grupo passou a ter dois elementos a partir de outubro de 2026.

**Relatórios** (`docs/relatorio/`): `relatorio_trabalho.pdf` descreve o trabalho
e `relatorio_ia.pdf` descreve o uso de ferramentas de IA. As fontes LaTeX estão
ao lado dos PDF.

---

## Compilar

```bash
sudo apt install -y build-essential cmake pkg-config libsndfile1-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Os executáveis ficam em `build/`. Para as comparações com o estado da arte são
precisos o `flac`, o `zstd`, o `xz-utils` e o `ffmpeg`, e para as figuras o
`numpy` e o `matplotlib`.

## Usar

| Invocação | Efeito |
|---|---|
| `wav_hist [-k K] [-o f.csv] in.wav canal` | Histograma de um canal (`0`, `1`, `mid` ou `side`), em bins de 2^K valores |
| `wav_cmp orig.wav mod.wav` | MSE, L∞ e SNR por canal e para o MID |
| `wav_quant -b BITS [-t] in.wav out.wav` | Quantização uniforme para BITS bits, ponto médio ou truncagem (`-t`) |
| `wav_effects in.wav out.wav efeito [par.]` | Efeitos `echo`, `multiecho`, `am` e `vdelay` |
| `wav_lossless_enc [-b N] [-r] [--threads T] in.wav comp` | Codifica sem perdas, blocos de N amostras, T fios |
| `wav_lossless_dec comp out.wav` | Descodifica e reproduz o original byte a byte |
| `wav_lossy_enc [-r KBPS \| -s DB \| -q PASSO] [-n N] [-z R] [-l] in.wav comp` | Codifica com perdas por débito, SNR ou passo fixo |
| `wav_lossy_dec comp out.wav` | Descodifica com perdas |

O ficheiro produzido pelo codec sem perdas não depende do número de fios, pelo
que `--threads` altera o tempo e não o resultado.

## Testes e verificação

```bash
./build/test_midside && ./build/test_bitstream && ./build/test_golomb && ./build/test_mdct
bash scripts/testa_lossless.sh        # 25 casos-limite do codec sem perdas
python3 scripts/verifica_numpy.py     # recálculo independente das métricas em numpy
```

## Resultados principais

Nos 20 ficheiros de treino, com o processador fixo em `performance` e o melhor
de três execuções.

- **Codec sem perdas.** Taxa total de 2,2838, contra 2,5675 do `flac -8`, 2,0360
  do `xz -9` e 1,8202 do `zstd -19`. Reproduz o original byte a byte e usa
  4,8 MiB de memória, independentemente da duração do áudio
  (`docs/resultados/medicoes.txt`).
- **Codec com perdas.** A 128 kbit/s, 45,97 dB de SNR médio, contra 21,64 dB do
  MP3 e 25,38 dB do AAC, em 0,34 s e com 5,4 MiB de memória
  (`docs/resultados/benchmark_lossy.csv`). O SNR favorece o nosso codec por
  construção, porque é a grandeza que ele minimiza, pelo que esta diferença não
  significa melhor qualidade percebida.
- **Parâmetros do codec com perdas.** Bloco N = 2048 e zona morta z = 0,40 são
  os melhores a 64 e a 128 kbit/s, e o MID/SIDE ortonormal ganha 1,4 a 1,7 dB
  (`docs/resultados/experiments_lossy.csv`).

## Estrutura

```
├── CMakeLists.txt            cada src/*.cpp dá origem a um programa
├── src/
│   ├── common/               base comum usada por todos os programas
│   │   ├── wav_io.hpp        ler e escrever WAV
│   │   ├── wav_stream.hpp    ler e escrever WAV em fluxo, sem libsndfile
│   │   ├── wav_raw.hpp       preservação do contentor do WAV original
│   │   ├── wav_header.hpp    cabeçalho de 44 bytes para os WAV novos
│   │   ├── channels.hpp      MID e SIDE
│   │   ├── bitstream.hpp     escrita e leitura bit a bit
│   │   ├── golomb.hpp        códigos de Golomb/Rice
│   │   ├── metrics.hpp       MSE, L-infinito e SNR
│   │   ├── mdct.hpp          MDCT rápida por FFT
│   │   ├── rangecoder.hpp    codificador aritmético adaptativo
│   │   └── lossy.hpp         partes comuns do codec com perdas
│   ├── wav_hist.cpp  wav_cmp.cpp  wav_quant.cpp  wav_effects.cpp
│   ├── wav_lossless_enc.cpp  wav_lossless_dec.cpp
│   ├── wav_lossy_enc.cpp     wav_lossy_dec.cpp
│   └── test_midside.cpp  test_bitstream.cpp  test_golomb.cpp  test_mdct.cpp
├── scripts/                  testes, comparações, varreduras e figuras
├── medicoes_relatorio.sh     medições usadas no Relatório 1
├── varredura_bloco.py        varredura do tamanho de bloco
├── analise_parte1.py         entropias e desvios padrão da Parte I
├── bench_hist.py             histograma em C++, Python e numpy
├── docs/
│   ├── relatorio/            relatórios em LaTeX, PDF e figuras
│   └── resultados/           medições que sustentam as tabelas dos relatórios
├── data/                     ficheiros .wav de teste (não vai para o Git)
└── build/                    programas compilados (não vai para o Git)
```

O `docs/resultados/LEIA-ME.md` diz o que cada ficheiro de resultados contém.
