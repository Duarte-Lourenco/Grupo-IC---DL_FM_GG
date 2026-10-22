# Resultados medidos

**Os números para o relatório são os desta pasta**, medidos nos **20 ficheiros de treino
oficiais** do eLearning: 10 do DSD100 (`*_mix.wav`) e 10 do EBU SQAM (`sqam_*.wav`), todos
WAV PCM de 16 bits, estéreo, 44,1 kHz. Os ficheiros de áudio não estão no Git; ficam em
`data/Audio_Datasets/DS100/` e `data/Audio_Datasets/TECH3253/`.

| Ficheiro | Parte | O que tem | Como se reproduz |
|---|---|---|---|
| `medicoes.txt` | II | Codec sem perdas contra `flac -8`, `flac -5`, `xz -9` e `zstd -19`: taxa, tempos e memória; versão antiga contra nova; ficheiro grande | `scripts/medir_tudo.py` |
| `benchmark_lossy.csv` | III | Codec com perdas contra MP3 (LAME) e AAC (ffmpeg) a 32, 64, 96, 128, 192 e 256 kbit/s: débito real, taxa, SNR, tempos e memória | `scripts/benchmark_lossy.sh data/Audio_Datasets/*/*.wav` |
| `experiments_lossy.csv` | III | SNR com cada tamanho de bloco N, arredondamento z e MID/SIDE ou L/R, a 64 e 128 kbit/s | `scripts/experiments_lossy.sh data/Audio_Datasets/*/*.wav` |
| `rice_vs_aritmetico.csv` | III | Fração de coeficientes nulos e tamanho com Rice (melhor k por bloco e canal) contra o codificador aritmético, com o mesmo Δ, a 64 e 128 kbit/s | ver `docs/parte3_codec_com_perdas.md`, secção 5.5.1 |
| `mdct_vs_dct2.csv` | III | SNR do mesmo codec com DCT-II em blocos independentes de 2048 amostras (para comparar com a MDCT, N = 2048, em `experiments_lossy.csv`), a 64 e 128 kbit/s | programa de experiência (não faz parte do codec) |
| `quant_oficial.csv` | I | `wav_quant` com 2 a 12 bits, ponto médio e truncar: MSE, L∞ e SNR por canal | `wav_quant` + `wav_cmp` em cada ficheiro |
| `lossy_antes_depois.csv` | III | Versão anterior (até ao PR #11) contra a versão eficiente, a 32, 64, 128 e 256 kbit/s | ver `docs/parte3_codec_com_perdas.md`, secção 18 |

A figura `docs/relatorio/figuras/rd_lossy.png` é gerada a partir de `benchmark_lossy.csv`
com `python3 scripts/plot_rd.py -i docs/resultados/benchmark_lossy.csv`.

## `preliminar_samples/`

Medições **preliminares**, feitas nos 7 ficheiros `sample01.wav` … `sample07.wav` antes de
haver os dados oficiais. Servem só como histórico (por exemplo, para mostrar que as
conclusões se mantiveram nos dois conjuntos); **não usar os números no relatório**.
O `benchmark_lossless.csv` desta pasta é ainda da 1.ª versão do codec sem perdas (12 out).

## Tempos e memória (dependem da máquina)

Os SNR, os tamanhos e os MSE acima são determinísticos e iguais em qualquer máquina. Os
**tempos e a memória** (e os SNR do MP3/AAC, que dependem da versão do `ffmpeg`) têm de
ser medidos na máquina de referência do relatório (Secção 2.3), com
`scripts/medir_parte3.sh`, que grava tudo em `results/parte3/` (`resumo.txt` tem as
tabelas prontas a copiar).
