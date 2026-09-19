# Information and Coding 2026/27 — Trabalho Prático n.º 1

| Pessoa | Nome | Parte I | Partes II / III |
|---|---|---|---|
| **A** | Duarte Lourenço | Base comum + `wav_hist` | Codec sem perdas (predição, formato do ficheiro, enc/dec) |
| **B** | Francisco Matos | `wav_cmp` + `wav_quant` | Codec com perdas (DCT, quantização) |
| **C** | Guilherme Gabino | `wav_effects` + BitStream + Golomb | Codificação entrópica + scripts de benchmark |

Entrega: **25 out 2026** · Discussão: **26 out 2026** · Enunciado: `trab1.pdf`

**Índice:**
[1. Estado](#1-estado-atual) ·
[2. Estrutura](#2-estrutura-do-repositório) ·
[3. Instalação](#3-instalação-primeira-vez-todos) ·
[4. Base comum](#4-a-base-comum-srccommon) ·
[5. Programas](#5-programas) ·
[6. Plano por pessoa](#6-plano-por-pessoa) ·
[7. Calendário](#7-calendário) ·
[8. Como trabalhamos](#8-como-trabalhamos-git) ·
[9. Problemas comuns](#9-problemas-comuns)

---

## 1. Estado atual

| Componente | Responsável | Estado |
|---|---|---|
| Base comum (`wav_io.hpp`, `channels.hpp`, CMake) | Duarte | ✅ feita (PR `base-comum`) |
| `test_midside` | Duarte | ✅ feito |
| `wav_hist` + `scripts/plot_hist.py` | Duarte | 🟡 feito, falta PR `wav-hist` |
| `wav_cmp` | Francisco | ⬜ por fazer |
| `wav_quant` | Francisco | ⬜ por fazer |
| `wav_effects` | Guilherme | ⬜ por fazer |
| `bitstream.hpp` + `golomb.hpp` | Guilherme | ⬜ por fazer |
| Codec sem perdas (Parte II) | Duarte + Guilherme | ⬜ |
| Codec com perdas (Parte III) | Francisco | ⬜ |
| Relatórios (`docs/relatorio/`) | Todos | ⬜ |

*(O Duarte atualiza esta tabela quando os PRs entram no `main`, para evitar conflitos.)*

---

## 2. Estrutura do repositório

```
Grupo-IC---DL_FM_GG/
├── CMakeLists.txt          ← instruções para compilar (cada src/*.cpp vira um programa)
├── README.md               ← este ficheiro
├── trab1.pdf               ← enunciado
├── src/
│   ├── common/             ← BASE COMUM, usada por todos os programas
│   │   ├── wav_io.hpp      ←   ler e escrever WAV
│   │   ├── channels.hpp    ←   MID e SIDE
│   │   ├── bitstream.hpp   ←   (Guilherme) escrever/ler bits
│   │   └── golomb.hpp      ←   (Guilherme) códigos de Golomb
│   ├── test_midside.cpp    ← teste da reconstrução MID/SIDE
│   ├── wav_hist.cpp        ← (Duarte)
│   ├── wav_cmp.cpp         ← (Francisco)
│   ├── wav_quant.cpp       ← (Francisco)
│   └── wav_effects.cpp     ← (Guilherme)
├── scripts/
│   └── plot_hist.py        ← gráficos dos histogramas
├── docs/
│   ├── ia_log.md           ← registo do uso de IA (Relatório 2)
│   └── relatorio/          ← relatórios em LaTeX
├── data/                   ← ficheiros .wav de teste   (NÃO vai para o Git)
└── build/                  ← programas compilados      (NÃO vai para o Git)
```

---

## 3. Instalação (primeira vez, todos)

Copiar e colar bloco a bloco no terminal.

**3.1 Instalar ferramentas**

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libsndfile1-dev git gh python3-pip alsa-utils
pip install numpy matplotlib
```

**3.2 Configurar o Git** (trocar pelo vosso nome e email)

```bash
git config --global user.name "O Teu Nome"
git config --global user.email "o.teu.email@ua.pt"
gh auth login        # GitHub.com → HTTPS → Yes → Login with a web browser
```

**3.3 Clonar o repositório** (Francisco e Guilherme; o Duarte já tem)

```bash
cd ~
git clone https://github.com/Duarte-Lourenco/Grupo-IC---DL_FM_GG.git
cd Grupo-IC---DL_FM_GG
```

**3.4 Ficheiros de áudio:** descarregar `sample01.wav` … `sample07.wav` do eLearning e pô-los na pasta `data/`:

```bash
mkdir -p data
mv ~/Transferências/sample0*.wav data/     # ajustar se a pasta de downloads for outra
ls data/                                   # devem aparecer os 7 ficheiros
```

**3.5 Compilar e testar**

```bash
cmake -B build            # só na primeira vez
cmake --build build       # sempre que alterarem código
./build/test_midside
./build/test_midside data/sample01.wav
```

As duas últimas linhas devem escrever `OK: ...`. Se sim, está tudo pronto.

---

## 4. A base comum (`src/common/`)

A base comum é uma **caixa de ferramentas**: não é um programa, são funções que todos os
programas usam. Sempre que precisarem de ler/escrever um WAV ou de calcular MID/SIDE,
**usem estas funções em vez de escrever a vossa versão**. Se faltar alguma coisa,
acrescenta-se aqui (por PR), para ficar disponível para os três.

### 4.1 Conceitos: o que é um ficheiro WAV

O som é uma onda. O computador mede a altura dessa onda muitas vezes por segundo; cada
medida é uma **amostra** (um número inteiro). Nos nossos ficheiros:

- **44100 amostras por segundo** (taxa de amostragem);
- **16 bits** por amostra → cada número vai de −32768 a 32767;
- **2 canais**, L (esquerdo) e R (direito) → em cada instante há 2 amostras (uma **frame**).

O `sample01.wav` tem 29,3 s → 29,3 × 44100 ≈ 1 294 188 frames = 2 588 376 números.

### 4.2 `wav_io.hpp` — ler e escrever WAV

O C++ não sabe o que é um ficheiro de áudio: um `.wav` é só uma sequência de bytes (um
cabeçalho + as amostras). Este ficheiro faz a conversão **bytes ⇄ números**, usando a
biblioteca libsndfile. **Não altera nada ao som**: quem altera são os programas, entre a
leitura e a escrita.

```cpp
WavData w = readWav("original.wav");     // 1. bytes → números
for (auto& s : w.samples)                // 2. o programa ALTERA os números (ex.: wav_quant)
    s = quantizar(s);
writeWav("quantizado.wav", w);           // 3. números → bytes
```

**A estrutura `WavData`** (o "contentor" de um ficheiro lido):

| Campo / função | Significado |
|---|---|
| `channels` | n.º de canais (2) |
| `sampleRate` | amostras por segundo (44100) |
| `bitsPerSample` | bits por amostra (16) |
| `format` | formato original (para gravar igual) |
| `samples` | **todas** as amostras, intercaladas: `L0 R0 L1 R1 L2 R2 …` |
| `frames()` | n.º de instantes (= `samples.size() / channels`) |
| `at(n, c)` | amostra do instante `n` no canal `c` (0 = L, 1 = R) |

Para escrever numa posição: `w.samples[n * w.channels + c] = valor;`

**`readWav(caminho)`**
1. Abre o ficheiro (se não existir, dá erro com mensagem clara).
2. Guarda canais, taxa, formato e bits.
3. Lê todas as amostras de uma vez.
4. **Corrige a escala:** a libsndfile entrega sempre os números "esticados" para 32 bits
   (1000 chega como 1000 × 65536). O `readWav` desfaz isso (`>> 16`) para todos
   trabalharmos com os valores **reais** do ficheiro (−32768…32767).

**`writeWav(caminho, dados)`** faz o inverso: volta a "esticar" os números e grava com o
mesmo formato.

| Programa | Lê WAV? | O que faz com os números | Grava WAV? |
|---|---|---|---|
| `wav_hist` | sim | conta quantas vezes aparece cada valor | não (CSV) |
| `wav_cmp` | sim (2) | compara e calcula erros / SNR | não (texto) |
| `wav_quant` | sim | reduz a precisão (menos bits) | **sim** |
| `wav_effects` | sim | eco, modulação, … | **sim** |
| codificadores | sim | comprimem | não (ficheiro comprimido) |
| descodificadores | não | descomprimem | **sim** |

### 4.3 `channels.hpp` — MID e SIDE

Numa música, L e R são quase iguais. Em vez dos dois, pode guardar-se a **média (MID)**,
que é praticamente o som todo, e a **diferença (SIDE)**, que é pequena. Números pequenos
comprimem melhor (Parte II).

| Função | Calcula | Para quê |
|---|---|---|
| `midOf(L, R)` | (L + R) / 2 | histograma do MID, codecs |
| `halfSideOf(L, R)` | (L − R) / 2 | o histograma pedido no enunciado |
| `sideOf(L, R)` | L − R | o que um codec guarda (sem perder informação) |
| `fromMidSide(MID, SIDE, L, R)` | L e R | o descodificador reconstrói o som |

**⚠️ Convenção do grupo — divisão inteira.** Dividir um número ímpar por 2 obriga a
arredondar, e em C++ há duas formas que dão resultados diferentes nos negativos:

| Conta | `/ 2` (arredonda para zero) | `>> 1` (arredonda para baixo) |
|---|---|---|
| 7 ÷ 2 | 3 | 3 |
| −3 ÷ 2 | **−1** | **−2** |

Se o codificador usasse uma e o descodificador outra, o áudio não voltava igual.
**Usamos sempre `>> 1`, e só através destas funções.** As contas intermédias são feitas
com `int64_t` para não haver *overflow*.

**Reconstrução exata (nota de rodapé 2 do enunciado).** Ao calcular o MID perde-se um bit
(7/2 e 6/2 dão ambos 3), mas esse bit está no SIDE: L + R e L − R são sempre ambos pares
ou ambos ímpares. Logo `L + R = 2·MID + (SIDE & 1)`.

Exemplo, L = 5, R = 2: MID = 7 >> 1 = 3, SIDE = 3.
L + R = 2·3 + 1 = 7 → L = (7 + 3) / 2 = **5**, R = 5 − 3 = **2** ✓

### 4.4 `test_midside.cpp` — o teste

- `./build/test_midside` → testa milhares de pares (L, R) em todo o intervalo de 16 bits,
  incluindo os casos perigosos (−32768, 32767, −1, 0, 1).
- `./build/test_midside data/sample01.wav` → testa todas as amostras reais do ficheiro.

Para cada par calcula MID e SIDE, reconstrói L e R e compara. Escreve `OK` ou mostra o
par que falhou. Também serve de **exemplo de como usar a base**.

### 4.5 `CMakeLists.txt` — compilar

O C++ tem de ser **compilado** (transformado num programa executável) antes de correr.
O `CMakeLists.txt` diz ao CMake como fazer isso:

| Linha | O que faz |
|---|---|
| `CMAKE_CXX_STANDARD 20` | C++20 (onde `>>` em negativos arredonda para baixo por definição) |
| `Release` / `-O2` | otimizações (o desempenho conta para a nota) |
| `-Wall -Wextra` | avisos sobre código suspeito |
| `pkg_check_modules(SNDFILE …)` | encontra a libsndfile (avisa se não estiver instalada) |
| `file(GLOB … src/*.cpp)` + `foreach` | **cada `src/NOME.cpp` vira o programa `build/NOME`**, já ligado à libsndfile |

**Programa novo = criar `src/nome.cpp` e compilar.** Ninguém precisa de editar o
`CMakeLists.txt` (assim não há conflitos no Git entre os três).

```bash
cmake -B build           # 1. prepara (só na primeira vez)
cmake --build build      # 2. compila tudo o que mudou → programas em build/
```

### 4.6 Como usar num programa novo

```cpp
#include "common/wav_io.hpp"      // readWav, writeWav
#include "common/channels.hpp"    // midOf, sideOf, ... (se precisares)

int main() {
    WavData w = readWav("data/sample01.wav");
    for (std::size_t n = 0; n < w.frames(); ++n) {
        int32_t L = w.at(n, 0), R = w.at(n, 1);
        // ... contas ...
    }
    writeWav("saida.wav", w);     // só se o programa gravar um WAV
}
```

```
ficheiro.wav ──readWav──► WavData (números)
                              ├─► midOf / sideOf ──► MID, SIDE ──fromMidSide──► L, R
                              ▼
                     (quantizar, efeitos, …)
                              │
                           writeWav ──► saida.wav
```

---
## 5. Programas

### 5.1 `wav_hist` — histogramas (Duarte) ✅

Conta quantas vezes aparece cada valor de amostra num canal.

```bash
./build/wav_hist [-k K] [-o saida.csv] entrada.wav canal
#   canal: 0 (L), 1 (R), mid = (L+R)/2, side = (L-R)/2
#   -k K : bins que juntam 2^K valores (0 = um bin por valor)

./build/wav_hist data/sample01.wav mid -o mid.csv
./build/wav_hist -k 4 data/sample01.wav side

# Gráficos de L, R, MID e SIDE (sem -o abre uma janela)
python3 scripts/plot_hist.py --wav data/sample01.wav
python3 scripts/plot_hist.py --wav data/sample01.wav -k 8 --log -o hist.png
```

O C++ calcula e escreve um CSV (`valor,contagem`); o Python só desenha.

### 5.2 `wav_cmp` — medir o erro (Francisco)

Compara um ficheiro com o original e imprime, **para cada canal e para o MID**:

| Métrica | Fórmula | Significado |
|---|---|---|
| MSE (norma L²) | média de (x − y)² | erro médio |
| L∞ | máximo de \|x − y\| | pior erro numa amostra |
| SNR | 10·log₁₀( Σx² / Σ(x − y)² ) dB | qualidade (maior = melhor) |

```bash
./build/wav_cmp original.wav modificado.wav
```

### 5.3 `wav_quant` — quantização uniforme (Francisco)

Reduz o n.º de bits úteis de cada amostra (ex.: de 16 para 8), gravando um WAV normal.
Ideia: descartar os `16 − b` bits menos significativos (truncar) ou arredondar para o
centro do intervalo (menor erro).

```bash
./build/wav_quant -b 8 data/sample01.wav /tmp/q8.wav
```

Resultado esperado: cada bit a menos baixa o SNR ≈ **6,02 dB**.

### 5.4 `wav_effects` — efeitos (Guilherme)

```bash
./build/wav_effects entrada.wav saida.wav efeito [parâmetros]
```

| Efeito | Parâmetros | Equação (D = atraso em amostras) |
|---|---|---|
| `echo` | atraso_ms ganho | y[n] = x[n] + a·x[n − D] |
| `multiecho` | atraso_ms ganho | y[n] = x[n] + a·y[n − D] (realimentação) |
| `am` | freq_hz | y[n] = x[n]·cos(2π·f·n / fs) |
| `vdelay` | atraso_max_ms freq_hz | y[n] = x[n] + a·x[n − D(n)], D(n) varia com um seno |

D = atraso_ms × 44100 / 1000. O resultado tem de ser limitado a −32768…32767.

### 5.5 `bitstream.hpp` e `golomb.hpp` (Guilherme)

Base dos codecs das Partes II e III.

- **BitStream:** escrever/ler bits individuais num ficheiro (o C++ só escreve bytes
  inteiros, por isso junta-se 8 bits num byte antes de escrever).
- **Golomb:** código para inteiros; valores pequenos → poucos bits. Com sinal usa-se
  primeiro o mapeamento *zigzag* (0, −1, 1, −2, 2 → 0, 1, 2, 3, 4). Cada valor é
  q = u / m (em unário) + r = u % m (em binário). Se m for potência de 2 chama-se código
  de Rice.

### 5.6 Codecs (Partes II e III)

```bash
./build/wav_lossless_enc [flags] input.wav comprimido
./build/wav_lossless_dec comprimido output.wav        # output.wav == input.wav (cmp)
./build/wav_lossy_enc [flags] input.wav comprimido
./build/wav_lossy_dec comprimido output.wav
```

Detalhes a definir quando a Parte I estiver fechada.

---

## 6. Plano por pessoa

> **Ordem importante:** o PR `base-comum` tem de entrar no `main` **antes** de o
> Francisco e o Guilherme começarem, porque os programas deles usam a base.
> O Francisco revê esse PR (ver 6.4).

Todos os blocos são para copiar e colar **dentro da pasta do repositório**
(`cd ~/Secretária/IC/Grupo-IC---DL_FM_GG` no caso do Duarte, `cd ~/Grupo-IC---DL_FM_GG`
nos outros).

### 6.1 Pessoa A — Duarte Lourenço

**Passo 1 — Atualizar o PR `base-comum`** (com este README e o novo `CMakeLists.txt`)

```bash
git switch base-comum
git add README.md CMakeLists.txt src/common/ src/test_midside.cpp
git status            # src/wav_hist.cpp e scripts/ NÃO devem estar em "Changes to be committed"
git commit -m "README com plano do grupo; CMake deteta os programas automaticamente"
git push
gh pr create --base main --fill 2>/dev/null || echo "O PR já existe — foi atualizado"
```

Pedir ao Francisco para rever e fazer merge (6.4).

**Passo 2 — PR `wav-hist`** (depois de `base-comum` estar no `main`)

```bash
git switch main
git pull
git switch -c wav-hist
cmake --build build
./build/wav_hist data/sample01.wav mid | head -5
python3 scripts/plot_hist.py --wav data/sample01.wav -k 4
git add src/wav_hist.cpp scripts/plot_hist.py
git commit -m "wav_hist: histogramas de L, R, MID e SIDE com bins de 2^k"
git push -u origin wav-hist
gh pr create --base main --fill
```

**Passo 3 — Material para o relatório:** gerar os gráficos para todos os ficheiros:

```bash
mkdir -p results
for f in data/*.wav; do
  n=$(basename "$f" .wav)
  python3 scripts/plot_hist.py --wav "$f" -o "results/hist_$n.png"
  python3 scripts/plot_hist.py --wav "$f" -k 8 --log -o "results/hist_${n}_k8.png"
done
```

**Depois (Parte II, a partir de 5/10):** codec sem perdas — preditores (fixos de ordem 0–3
e/ou LPC), MID/SIDE adaptativo, formato do ficheiro comprimido, programas
`wav_lossless_enc` / `wav_lossless_dec`, usando o BitStream e o Golomb do Guilherme.

### 6.2 Pessoa B — Francisco Matos

**Passo 1 — Rever o PR `base-comum` do Duarte** (ver 6.4).

**Passo 2 — `wav_cmp`**

```bash
git switch main
git pull
git switch -c wav-cmp
cat > src/wav_cmp.cpp <<'EOF_CPP'
// wav_cmp — compara um ficheiro WAV com o original.
// Uso: wav_cmp original.wav modificado.wav
#include "common/channels.hpp"
#include "common/wav_io.hpp"

#include <cmath>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Uso: wav_cmp original.wav modificado.wav\n";
        return 1;
    }
    WavData orig = readWav(argv[1]);
    WavData mod = readWav(argv[2]);

    // TODO: verificar que os dois têm o mesmo n.º de canais e de frames
    // TODO: para cada canal c (e para o MID = midOf(L, R)):
    //   erro = orig.at(n, c) - mod.at(n, c)
    //   MSE  = média de erro^2                          (norma L2)
    //   Linf = máximo de |erro|                         (norma L-infinito)
    //   SNR  = 10 * log10( soma(orig^2) / soma(erro^2) )  em dB
    // Usar double/int64 nas somas para não haver overflow.

    std::cout << "frames: " << orig.frames() << " / " << mod.frames() << "\n";
    return 0;
}
EOF_CPP
cmake -B build && cmake --build build
./build/wav_cmp data/sample01.wav data/sample01.wav
```

Implementar os `TODO` em `src/wav_cmp.cpp`. Teste: comparar um ficheiro consigo próprio
tem de dar **MSE = 0, L∞ = 0, SNR = infinito**.

```bash
cmake --build build && ./build/wav_cmp data/sample01.wav data/sample01.wav
git add src/wav_cmp.cpp
git commit -m "wav_cmp: MSE, L-infinito e SNR por canal e MID"
git push -u origin wav-cmp
gh pr create --base main --fill
```

**Passo 3 — `wav_quant`** (pode começar logo a seguir, noutro branch)

```bash
git switch main
git pull
git switch -c wav-quant
cat > src/wav_quant.cpp <<'EOF_CPP'
// wav_quant — quantização escalar uniforme (reduz o n.º de bits por amostra).
// Uso: wav_quant -b BITS entrada.wav saida.wav
#include "common/wav_io.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 5 || std::string(argv[1]) != "-b") {
        std::cerr << "Uso: wav_quant -b BITS entrada.wav saida.wav\n";
        return 1;
    }
    int bits = std::atoi(argv[2]);
    WavData w = readWav(argv[3]);

    // TODO: validar 1 <= bits <= w.bitsPerSample
    // TODO: descartar = w.bitsPerSample - bits
    //   para cada amostra s em w.samples:
    //     versão 1 (truncar):     s = (s >> descartar) << descartar;
    //     versão 2 (arredondar):  ponto médio do intervalo -> menor erro
    // O ficheiro de saída continua a ter o mesmo formato (16 bits).

    std::cout << "bits pedidos: " << bits << "\n";
    writeWav(argv[4], w);
    return 0;
}
EOF_CPP
cmake --build build
```

Implementar os `TODO`. Teste (quando o `wav_cmp` já estiver no `main`):

```bash
cmake --build build
for b in 16 12 10 8 6 4 2; do
  ./build/wav_quant -b $b data/sample01.wav /tmp/q$b.wav
  echo "=== $b bits"; ./build/wav_cmp data/sample01.wav /tmp/q$b.wav
done
aplay /tmp/q4.wav        # ouvir o ruído de quantização
```

Com 16 bits o erro tem de ser 0; cada bit a menos ≈ −6 dB de SNR. Guardar estes
resultados numa tabela para o relatório.

```bash
git add src/wav_quant.cpp
git commit -m "wav_quant: quantização escalar uniforme"
git push -u origin wav-quant
gh pr create --base main --fill
```

**Depois (Parte III, a partir de 12/10):** codec com perdas baseado na DCT — blocos,
DCT, quantização dos coeficientes, `wav_lossy_enc` / `wav_lossy_dec`, e comparação
com MP3/AAC (SNR vs. débito) usando o `wav_cmp`.

### 6.3 Pessoa C — Guilherme Gabino

**Passo 1 — `wav_effects`**

```bash
git switch main
git pull
git switch -c wav-effects
cat > src/wav_effects.cpp <<'EOF_CPP'
// wav_effects — efeitos de áudio.
// Uso: wav_effects entrada.wav saida.wav efeito [parâmetros]
//   echo      atraso_ms ganho
//   multiecho atraso_ms ganho
//   am        freq_hz
//   vdelay    atraso_max_ms freq_hz
#include "common/wav_io.hpp"

#include <cmath>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Uso: wav_effects entrada.wav saida.wav efeito [parâmetros]\n";
        return 1;
    }
    WavData in = readWav(argv[1]);
    WavData out = in;               // mesmo formato; as amostras vão ser substituídas
    std::string efeito = argv[3];

    // D = atraso em amostras = atraso_ms * in.sampleRate / 1000
    // TODO echo:      y[n] = x[n] + a * x[n - D]
    // TODO multiecho: y[n] = x[n] + a * y[n - D]     (realimentação)
    // TODO am:        y[n] = x[n] * cos(2*pi*f*n / fs)
    // TODO vdelay:    y[n] = x[n] + a * x[n - D(n)],  D(n) varia com um seno
    // Fazer para cada canal c com in.at(n, c); escrever em out.samples[n * channels + c].
    // Limitar o resultado ao intervalo válido (-32768..32767 em 16 bits).

    std::cout << "efeito: " << efeito << "\n";
    writeWav(argv[2], out);
    return 0;
}
EOF_CPP
cmake -B build && cmake --build build
```

Implementar os `TODO` (um efeito de cada vez). Testar a ouvir:

```bash
cmake --build build
./build/wav_effects data/sample02.wav /tmp/echo.wav echo 300 0.5 && aplay /tmp/echo.wav
./build/wav_effects data/sample02.wav /tmp/multi.wav multiecho 250 0.6 && aplay /tmp/multi.wav
./build/wav_effects data/sample02.wav /tmp/am.wav am 5 && aplay /tmp/am.wav
./build/wav_effects data/sample02.wav /tmp/vd.wav vdelay 10 0.5 && aplay /tmp/vd.wav
```

```bash
git add src/wav_effects.cpp
git commit -m "wav_effects: eco, ecos múltiplos, AM e atraso variável"
git push -u origin wav-effects
gh pr create --base main --fill
```

**Passo 2 — BitStream** (base da Parte II; é a peça mais importante para os codecs)

```bash
git switch main
git pull
git switch -c bitstream
cat > src/common/bitstream.hpp <<'EOF_CPP'
// bitstream.hpp — escrita e leitura de ficheiros bit a bit.
// Usado pelos codecs das Partes II e III.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>

class BitWriter {
public:
    explicit BitWriter(const std::string& path) : out_(path, std::ios::binary) {}
    ~BitWriter() { flush(); }

    void writeBit(int bit) {
        // TODO: acumular o bit em buffer_; quando tiver 8 bits, escrever 1 byte
        (void)bit;
    }
    void writeBits(uint64_t value, int n) {
        // TODO: escrever os n bits menos significativos de value (do mais significativo para o menos)
        (void)value; (void)n;
    }
    void flush() {
        // TODO: escrever os bits que faltam, completando o último byte com zeros
    }

private:
    std::ofstream out_;
    uint8_t buffer_ = 0;
    int count_ = 0;   // n.º de bits em buffer_
};

class BitReader {
public:
    explicit BitReader(const std::string& path) : in_(path, std::ios::binary) {}

    int readBit() {
        // TODO: ler 1 byte quando o buffer estiver vazio; devolver o bit seguinte
        return 0;
    }
    uint64_t readBits(int n) {
        // TODO: ler n bits e juntá-los num inteiro
        (void)n;
        return 0;
    }

private:
    std::ifstream in_;
    uint8_t buffer_ = 0;
    int count_ = 0;
};
EOF_CPP
cat > src/test_bitstream.cpp <<'EOF_CPP'
// test_bitstream — escreve bits num ficheiro, lê-os de volta e compara.
#include "common/bitstream.hpp"

#include <iostream>

int main() {
    {
        BitWriter w("/tmp/test_bitstream.bin");
        w.writeBit(1);
        w.writeBit(0);
        w.writeBits(0b10110, 5);
        w.writeBits(123456, 20);
    }   // o destrutor faz flush
    BitReader r("/tmp/test_bitstream.bin");
    bool ok = r.readBit() == 1 && r.readBit() == 0 &&
              r.readBits(5) == 0b10110 && r.readBits(20) == 123456;
    std::cout << (ok ? "OK: bitstream\n" : "FALHOU: bitstream\n");
    return ok ? 0 : 1;
}
EOF_CPP
cmake --build build && ./build/test_bitstream
```

Até implementarem os `TODO`, o teste escreve `FALHOU` (é normal). Quando der `OK`:

```bash
git add src/common/bitstream.hpp src/test_bitstream.cpp
git commit -m "BitStream: escrita e leitura bit a bit"
git push -u origin bitstream
gh pr create --base main --fill
```

**Passo 3 — Golomb** (depois de o BitStream estar no `main`)

```bash
git switch main
git pull
git switch -c golomb
cat > src/common/golomb.hpp <<'EOF_CPP'
// golomb.hpp — códigos de Golomb para inteiros com sinal.
// Usado pelos codecs das Partes II e III.
#pragma once

#include "common/bitstream.hpp"

#include <cstdint>

// Inteiro com sinal -> sem sinal:  0,-1,1,-2,2,... -> 0,1,2,3,4,...
inline uint64_t zigzag(int64_t v) { return v >= 0 ? 2 * static_cast<uint64_t>(v) : 2 * static_cast<uint64_t>(-v) - 1; }
inline int64_t unzigzag(uint64_t u) { return (u & 1) ? -static_cast<int64_t>((u + 1) / 2) : static_cast<int64_t>(u / 2); }

class Golomb {
public:
    explicit Golomb(uint32_t m) : m_(m) {}

    void encode(BitWriter& w, int64_t value) const {
        // TODO: u = zigzag(value); q = u / m; r = u % m
        //   q em código unário (q uns seguidos de um zero)
        //   r em binário truncado (se m for potência de 2: log2(m) bits -> código de Rice)
        (void)w; (void)value;
    }
    int64_t decode(BitReader& r) const {
        // TODO: operação inversa de encode
        (void)r;
        return 0;
    }

private:
    uint32_t m_;
};
EOF_CPP
cat > src/test_golomb.cpp <<'EOF_CPP'
// test_golomb — codifica valores, descodifica-os e compara (vários m).
#include "common/golomb.hpp"

#include <iostream>
#include <vector>

int main() {
    std::vector<int64_t> vals = {0, 1, -1, 2, -2, 7, -8, 100, -1000, 32767, -32768};
    for (uint32_t m : {1u, 2u, 3u, 4u, 5u, 8u, 16u, 100u}) {
        {
            BitWriter w("/tmp/test_golomb.bin");
            Golomb g(m);
            for (auto v : vals) g.encode(w, v);
        }
        BitReader r("/tmp/test_golomb.bin");
        Golomb g(m);
        for (auto v : vals) {
            int64_t d = g.decode(r);
            if (d != v) {
                std::cout << "FALHOU: m=" << m << " valor=" << v << " descodificado=" << d << "\n";
                return 1;
            }
        }
    }
    std::cout << "OK: golomb\n";
    return 0;
}
EOF_CPP
cmake --build build && ./build/test_golomb
```

Quando o teste der `OK` (tem de funcionar para qualquer `m`, não só potências de 2):

```bash
git add src/common/golomb.hpp src/test_golomb.cpp
git commit -m "Golomb: codificação de inteiros com sinal (zigzag)"
git push -u origin golomb
gh pr create --base main --fill
```

**Depois (Partes II e III):** escolha adaptativa do parâmetro `m`, integração no codec
do Duarte, e `scripts/benchmark.sh` para comparar com `flac`, `zstd`, `xz` (e mais
tarde `ffmpeg` MP3/AAC), medindo tempo e memória com `/usr/bin/time -v`.

### 6.4 Rever um PR de um colega (todos)

```bash
gh pr list                          # ver os PRs abertos e o número de cada
gh pr checkout NUMERO               # trazer o código do PR para o teu computador
cmake -B build && cmake --build build
# correr os testes / exemplos indicados no PR
gh pr review NUMERO --approve       # ou: --request-changes -b "o que falta"
gh pr merge NUMERO --merge --delete-branch
git switch main && git pull         # voltar ao main atualizado
```

Também se pode fazer tudo no site: separador **Pull requests** → *Files changed* →
*Review changes* → *Approve* → *Merge pull request*.

---

## 7. Calendário

| Semana | Datas | Duarte (A) | Francisco (B) | Guilherme (C) |
|---|---|---|---|---|
| 1 | 19/9 – 27/9 | PR base-comum, PR wav-hist | rever base, `wav_cmp` | `wav_effects` |
| 2 | 28/9 – 4/10 | gráficos, texto da Parte I | `wav_quant` + tabela SNR | BitStream + Golomb |
| 3 | 5/10 – 11/10 | codec sem perdas | começar DCT | Golomb adaptativo, benchmarks |
| 4 | 12/10 – 18/10 | comparação FLAC/zstd/xz | codec com perdas | benchmarks MP3/AAC |
| 5 | 19/10 – 25/10 | relatório | relatório | relatório |

Escrever no relatório **à medida que cada parte fica pronta**, não só na última semana.

---
## 8. Como trabalhamos (Git)

**Regra principal: ninguém faz commit diretamente no `main`.**
O `main` tem sempre código que compila e funciona. Cada tarefa é feita num *branch* próprio e só entra no `main` através de um *pull request* (PR) revisto por outro membro.

### Ciclo de uma tarefa

1. **Atualizar o `main`** antes de começar:
   ```bash
   git switch main
   git pull
   ```
2. **Criar um branch** para a tarefa, com um nome curto e descritivo:
   ```bash
   git switch -c wav-hist        # exemplos: wav-cmp, bitstream, golomb, lossless-enc
   ```
3. **Trabalhar e fazer commits pequenos**, com mensagens claras:
   ```bash
   git add ficheiro1 ficheiro2
   git commit -m "wav_hist: histograma do canal MID"
   ```
4. **Enviar o branch** para o GitHub:
   ```bash
   git push -u origin wav-hist   # a partir daqui basta "git push"
   ```
5. **Abrir um pull request**, com uma descrição curta do que foi feito e de como testar:
   ```bash
   gh pr create --base main --fill      # ou no site: botão "Compare & pull request"
   ```
6. **Revisão:** outro membro lê o código e testa-o (ver [6.4](#64-rever-um-pr-de-um-colega-todos)). Se estiver tudo bem, aprova e faz *merge*.
7. **Limpar** depois do merge:
   ```bash
   git switch main
   git pull
   git branch -d wav-hist
   ```

### Se o `main` mudou enquanto trabalhavas

```bash
git switch wav-hist
git merge main          # resolver conflitos, se houver, e fazer commit
```

### Regras do grupo

- Um branch = uma tarefa. Branches curtos são mais fáceis de rever.
- Antes de abrir um PR, confirmar que o código compila e que os testes passam.
- Os ficheiros de áudio (`data/`) e os binários compilados (`build/`) não vão para o Git (ver `.gitignore`).
- Nunca editar os ficheiros de `src/common/` de outra pessoa sem avisar: são usados por todos.

### Registo do uso de IA (`docs/ia_log.md`)

Cada utilização de IA fica registada — é a base do Relatório 2. Copiar este modelo para o
fim do ficheiro e preencher:

```markdown
### AAAA-MM-DD — Nome — Ferramenta (ex.: Claude, ChatGPT, Copilot)
- **Problema:**
- **O que a IA propôs:**
- **Como verificámos:**
- **Estava correto?**
- **O que alterámos:**
- **O que aprendemos:**
```

---

## 9. Problemas comuns

| Erro | Solução |
|---|---|
| `Package 'sndfile' not found` | `sudo apt install libsndfile1-dev pkg-config` e depois `rm -rf build && cmake -B build` |
| `./build/xxx: No such file or directory` | falta compilar: `cmake --build build` (ou `cmake -B build` se a pasta `build` não existir) |
| Programa novo não aparece em `build/` | confirmar que o ficheiro está em `src/` e termina em `.cpp`; depois `cmake -B build && cmake --build build` |
| `Erro ao abrir 'data/...'` | os ficheiros de áudio não estão em `data/` (ver 3.4) |
| `Permission denied` / pede password no `git push` | `gh auth login` (ver 3.2) |
| `git pull` diz *CONFLICT* | abrir o ficheiro, escolher o código certo entre `<<<<<<<` e `>>>>>>>`, apagar os marcadores, `git add ficheiro`, `git commit` |
| Fiz commit no `main` por engano (sem push) | `git switch -c nome-do-branch` (leva o commit), depois `git switch main && git reset --hard origin/main` |
| `aplay` não toca nada | abrir o `.wav` no VLC ou noutro leitor |
