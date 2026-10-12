# Parte III — Codec de áudio com perdas baseado na DCT

**Documento interno do grupo. Explica tudo o que foi feito na Parte III do trabalho.**

Information and Coding 2026/27 — Trabalho Prático n.º 1
Responsável pela Parte III: Francisco Matos · Estado em 12 out 2026

Este documento não é o relatório a entregar, que tem no máximo 12 páginas e não pode
ter código. Serve para nos orientarmos e para prepararmos a discussão. Está tudo aqui:
- o que existe e para que serve;
- que decisões tomámos e porquê;
- o que medimos;
- que problemas apareceram e como os resolvemos.

A partir daqui escreve-se a secção da Parte III do Relatório 1, e a entrada de IA do
Relatório 2 sai da secção 15.

---

## Índice

1. [O que o enunciado pede](#1-o-que-o-enunciado-pede)
2. [Ponto de partida: o que já tínhamos](#2-ponto-de-partida-o-que-já-tínhamos)
3. [Visão geral do codec](#3-visão-geral-do-codec)
4. [Conceitos de base](#4-conceitos-de-base)
5. [Os blocos do codec, um a um](#5-os-blocos-do-codec-um-a-um)
   - 5.1 [`metrics.hpp` — medidas de erro partilhadas](#51-metricshpp--medidas-de-erro-partilhadas)
   - 5.2 [MID/SIDE ortonormal](#52-midside-ortonormal)
   - 5.3 [A transformada: MDCT](#53-a-transformada-mdct)
   - 5.4 [Quantização](#54-quantização)
   - 5.5 [Codificação entrópica: codificador aritmético](#55-codificação-entrópica-codificador-aritmético)
   - 5.6 [Controlo do débito: `-q`, `-r` e `-s`](#56-controlo-do-débito--q--r-e--s)
   - 5.7 [Formato do ficheiro comprimido](#57-formato-do-ficheiro-comprimido)
   - 5.8 [Início e fim do ficheiro e casos especiais](#58-início-e-fim-do-ficheiro-e-casos-especiais)
   - 5.9 [Memória: síntese bloco a bloco](#59-memória-síntese-bloco-a-bloco)
6. [Ficheiros criados ou alterados](#6-ficheiros-criados-ou-alterados)
7. [Manual de utilização](#7-manual-de-utilização)
8. [Testes e verificação](#8-testes-e-verificação)
9. [Experiências: justificação dos parâmetros](#9-experiências-justificação-dos-parâmetros)
10. [Comparação com MP3 e AAC](#10-comparação-com-mp3-e-aac)
11. [Problemas encontrados e como foram resolvidos](#11-problemas-encontrados-e-como-foram-resolvidos)
12. [Evolução: primeira versão vs. versão final](#12-evolução-primeira-versão-vs-versão-final)
13. [Limitações e trabalho futuro](#13-limitações-e-trabalho-futuro)
14. [Perguntas prováveis na discussão](#14-perguntas-prováveis-na-discussão)
15. [Uso de IA nesta parte](#15-uso-de-ia-nesta-parte)
16. [Como reproduzir tudo](#16-como-reproduzir-tudo)
17. [Estado no Git e próximos passos](#17-estado-no-git-e-próximos-passos)

---

## 1. O que o enunciado pede

A Parte III tem dois pontos (`trab1.pdf`, p. 2–3).

**Ponto 7.** *"Develop a lossy audio codec aiming at the best possible rate-distortion
performance. The codec must be based on the Discrete Cosine Transform (DCT). All other
design decisions are left to the group and must be experimentally justified."*

O codec tem de ser chamado assim:

```
wav_lossy_enc [ optional flags ] input.wav compressed_file
wav_lossy_dec compressed_file output.wav
```

**Ponto 8.** *"Your codec should be compared with state-of-the-art lossy codecs, such as
those of the MPEG family. This comparison should include the compression ratio and the
corresponding SNR, but also compression/decompression time and memory usage."*

O que isto quer dizer em concreto:

| Exigência | Como a cumprimos |
|---|---|
| Baseado na DCT | MDCT, que é uma DCT-IV aplicada a blocos sobrepostos (secção 5.3) |
| Melhor relação débito-distorção | Medimos a distorção com o SNR (o critério que o enunciado usa na comparação) e otimizámos tudo para ele |
| Decisões justificadas com experiências | `scripts/experiments_lossy.sh` (secção 9) e medições da secção 5.5 |
| Formato de chamada | Igual ao pedido; as opções vêm antes dos ficheiros |
| Comparar com codecs MPEG | MP3 (LAME) e AAC (ffmpeg), em 6 débitos e nos 7 ficheiros (secção 10) |
| Taxa, SNR, tempo e memória | Tudo no CSV `docs/resultados/benchmark_lossy.csv` |

Há também exigências gerais que valem para esta parte:
- correr em Linux, ser fácil de instalar e ter exemplos de uso no README. O CMake já
  existia e os programas novos são apanhados automaticamente;
- o desempenho também conta tempo e memória (Intro, 1.º ponto), por isso reduzimos a
  memória (secção 5.9);
- não especializar o codec nos nossos ficheiros, porque o professor testa com dados
  privados. Os parâmetros escolhidos são gerais (secção 9).

---

## 2. Ponto de partida: o que já tínhamos

Antes de começar a Parte III, o repositório tinha:

| O que existia | Quem fez | Como foi usado na Parte III |
|---|---|---|
| `wav_io.hpp`: leitura e escrita de WAV com libsndfile | Duarte | Ler o WAV no codificador e escrever o WAV no descodificador |
| `channels.hpp`: MID/SIDE inteiro, exatamente reversível | Duarte | Não foi usado diretamente; o codec com perdas usa uma versão real e ortonormal (5.2) |
| `wav_cmp`: MSE, L∞ e SNR | Francisco | As contas passaram para `metrics.hpp` e são usadas também dentro do codificador (5.1) |
| `wav_quant`: quantização escalar uniforme | Francisco | A mesma ideia, aplicada aos coeficientes da MDCT em vez das amostras (5.4) |
| `bitstream.hpp` e `golomb.hpp` (Rice) | Duarte | Testados como alternativa para a codificação entrópica: ocupavam 1,9× a 3,9× mais (5.5) |
| Codec sem perdas e `benchmark_lossless.sh` | Duarte | Serviu de modelo de estilo para o `benchmark_lossy.sh` e de referência para o tamanho (5.5) |
| CMake que compila cada `src/*.cpp` | Duarte | Os novos programas e o `test_mdct` são compilados sem tocar no CMake |

Para a Parte III em si **não havia nada implementado**. Só existia o plano no README:
- "codec com perdas baseado na DCT — blocos, DCT, quantização dos coeficientes,
  `wav_lossy_enc` / `wav_lossy_dec`, e comparação com MP3/AAC (SNR vs. débito) usando o
  `wav_cmp`";
- extrair um `metrics.hpp` do `wav_cmp`.

Contexto do grupo: a partir de outubro passámos a ser dois, e a Parte III ficou
inteira com o Francisco.

**Dados de teste:** os 7 ficheiros do eLearning, todos estéreo, 44 100 Hz, 16 bits:

| Ficheiro | Frames | Duração | Bytes |
|---|---|---|---|
| sample01 | 1 294 188 | 29,3 s | 5 176 796 |
| sample02 | 647 388 | 14,7 s | 2 589 596 |
| sample03 | 882 588 | 20,0 s | 3 530 396 |
| sample04 | 588 588 | 13,3 s | 2 354 396 |
| sample05 | 911 988 | 20,7 s | 3 647 996 |
| sample06 | 1 058 988 | 24,0 s | 4 235 996 |
| sample07 | 941 388 | 21,3 s | 3 765 596 |

---

## 3. Visão geral do codec

### 3.1 A cadeia

```
CODIFICADOR (wav_lossy_enc)

 input.wav
    │  readWav (wav_io.hpp)
    ▼
 amostras inteiras L, R
    │  MID/SIDE ortonormal:  M = (L+R)/√2,  S = (L−R)/√2         [5.2]
    ▼
 canais M, S (reais)
    │  MDCT, janela seno, blocos de 2N amostras com avanço N        [5.3]
    ▼
 N coeficientes por bloco e por canal (guardados em float)
    │  quantização uniforme com zona morta, passo Δ                 [5.4]
    │  (Δ escolhido por bisseção para o débito ou SNR pedido)        [5.6]
    ▼
 inteiros q (70–88 % são zero)
    │  codificador aritmético adaptativo com contexto               [5.5]
    ▼
 cabeçalho (29 bytes) + bytes do codificador aritmético              [5.7]
    ▼
 compressed_file


DESCODIFICADOR (wav_lossy_dec) — o caminho inverso, bloco a bloco

 compressed_file → cabeçalho → descodificador aritmético → q
   → q·Δ → MDCT inversa → sobreposição e soma com o bloco anterior
   → L = (M+S)/√2, R = (M−S)/√2 → arredondar e limitar ao intervalo → output.wav
```

### 3.2 A ideia em poucas linhas

1. **Transformar.** O áudio "no tempo" tem a energia espalhada por todas as amostras.
   Depois da MDCT, a energia concentra-se em poucos coeficientes (sobretudo nas
   baixas frequências), e a maior parte fica perto de zero.
2. **Quantizar.** Dividimos cada coeficiente por Δ e ficamos só com a parte inteira
   (com uma regra de arredondamento, secção 5.4). É **o único passo com perdas**. Os
   coeficientes pequenos passam a 0.
3. **Codificar.** Escrevemos os inteiros com o menor número de bits possível. Um
   modelo estatístico adaptativo prevê, coeficiente a coeficiente, a probabilidade de
   ser 0, 1, 2… e o codificador aritmético gasta −log₂(p) bits.
4. **Um só botão: Δ.** Δ maior dá menos bits e mais erro; Δ menor dá o contrário.
   O utilizador pede um débito ou um SNR e o codificador procura o Δ certo.

---

## 4. Conceitos de base

Esta secção explica o vocabulário necessário para perceber as decisões. Convém saber
explicá-la de cor na discussão.

### 4.1 Débito, taxa de compressão e distorção

- **Débito (bitrate):** bits do ficheiro comprimido por segundo de áudio, em kbit/s.
  O WAV original tem 44 100 × 16 × 2 = **1411,2 kbit/s**. A 128 kbit/s comprimimos
  ~11×.
- **Taxa de compressão:** bytes do original / bytes do comprimido. A 128 kbit/s é
  11,03 nos nossos ficheiros.
- **Distorção:** quanto o áudio descodificado difere do original. Usamos o SNR
  (definição na 4.2).
- **Curva débito-distorção (rate-distortion, RD):** para cada débito, qual é o SNR
  conseguido. Um codec é melhor do que outro se a sua curva estiver **acima** (mais
  SNR ao mesmo débito) ou **à esquerda** (menos débito para o mesmo SNR). O enunciado
  pede exatamente a melhor curva possível.

### 4.2 SNR, MSE e L∞ (o que o `wav_cmp` mede)

Com x = original e y = descodificado:
- MSE = média de (x − y)²
- L∞ = máximo de |x − y|
- SNR = 10·log₁₀( Σx² / Σ(x − y)² ) dB

Cada +6,02 dB no SNR equivale a dividir o erro (em amplitude) por 2. Na quantização
da Parte I medimos exatamente isso: cada bit a menos tira ~6,02 dB.

### 4.3 Porquê transformar antes de quantizar

Se quantizássemos as amostras diretamente (como o `wav_quant`), cada amostra precisava
de praticamente o mesmo número de bits, porque todas têm amplitudes parecidas. A 128
kbit/s há 128 000 / 88 200 ≈ 1,45 bits por amostra, e pela regra dos ~6 dB por bit o SNR
ficaria por volta de 9 dB. Com a transformada obtemos ~34 dB ao mesmo débito.

Depois de uma transformada como a DCT, o sinal fica descrito por coeficientes de
amplitudes **muito diferentes**: poucos grandes e muitos quase nulos. A isto chama-se
**compactação de energia**. Os quase nulos passam a 0 e custam quase nada. Os bits vão
para onde está a energia. É por isso que todos os codecs de áudio modernos (MP3, AAC,
Vorbis, Opus) e de imagem (JPEG) usam uma transformada deste tipo.

### 4.4 Transformada ortonormal (teorema de Parseval)

Uma transformada é **ortonormal** se conserva a energia: Σ(sinal)² = Σ(coeficientes)².
A consequência que nos interessa é que **o erro também se conserva**. Se a
quantização introduz um erro com energia E nos coeficientes, o áudio reconstruído tem
um erro com a mesma energia E.

Isto é central no nosso desenho:
- podemos raciocinar sobre o erro no domínio dos coeficientes e saber logo qual vai
  ser o SNR no áudio;
- a bisseção do `-s` (SNR alvo) usa isto (secção 5.6).

Tivemos o cuidado de tornar **todas** as etapas ortonormais: o MID/SIDE com √2 em vez
de /2, a MDCT com janela seno e escala √(2/N).

### 4.5 Quantização uniforme e o erro de Δ²/12

Um quantizador uniforme com passo Δ e arredondamento ao mais próximo comete um erro
entre −Δ/2 e +Δ/2. Para sinais "ricos", esse erro tem MSE ≈ Δ²/12. Já o verificámos
na Parte I: o MSE medido no `wav_quant` coincidiu com Δ²/12.

---

## 5. Os blocos do codec, um a um

Para cada bloco: o que é, para que serve, que alternativas havia, o que escolhemos e
porquê.

### 5.1 `metrics.hpp` — medidas de erro partilhadas

**O que é.** O ficheiro `src/common/metrics.hpp`, com a estrutura `Stats`. Vai somando
Σx², Σ(x−y)² e o máximo de |x−y|, e calcula o MSE, o L∞ e o SNR.

**O que tínhamos antes.** Estas contas estavam escritas dentro de `src/wav_cmp.cpp`.

**O que mudou.**
- A estrutura passou para a base comum.
- Ganhou as funções `mse()` e `snr()` (devolvem +∞ se não há erro e −∞ se o original
  é silêncio mas há erro).
- O `wav_cmp` passou a incluí-la.
- O resultado do `wav_cmp` ficou **exatamente igual**. Confirmámos com o mesmo par de
  ficheiros antes e depois: MID com SNR de 12,8853 dB nas duas versões.

**Porque precisamos disto.**
- O codificador com perdas imprime, no fim, o SNR real que obteve. Para isso compara o
  original com o que o descodificador vai produzir, e usa **as mesmas contas** do
  `wav_cmp`.
- Assim o número que o codificador mostra é diretamente comparável ao do `wav_cmp`.
  Não há duas implementações que possam divergir.
- Era um ponto do plano do README.

### 5.2 MID/SIDE ortonormal

**O que é.** Em estéreo, em vez de codificar L e R codificamos:

```
M = (L + R) / √2        S = (L − R) / √2
L = (M + S) / √2        R = (M − S) / √2
```

**Para que serve.** Na música, L e R são muito parecidos (correlacionados). M fica com
quase toda a energia e S é pequeno. Um S pequeno tem muitos coeficientes quantizados
a 0, que custam poucos bits.

**Porquê √2 e não /2 como no `channels.hpp`.**
- O `channels.hpp` do Duarte faz MID = (L+R)>>1 e SIDE = L−R em inteiros, porque o codec
  **sem perdas** precisa de reconstrução exata, bit a bit.
- No codec com perdas não precisamos disso: vamos quantizar de qualquer forma. O que
  precisamos é que a transformação seja **ortonormal** (secção 4.4). Com √2, a matriz
  [[1,1],[1,−1]]/√2 é uma rotação de 45°, que conserva a energia e o erro.
- Com /2 (ou sem divisão) o erro de quantização em M e S seria ampliado ou reduzido de
  forma diferente ao voltar a L/R. Usar o mesmo Δ nos dois canais deixava de ser ótimo.

**Medição.** O ganho é pequeno, mas o MID/SIDE nunca fica pior. Média dos 7 ficheiros:

| Débito | MID/SIDE | L/R | Ganho |
|---|---|---|---|
| 64 kbit/s | 25,13 dB | 25,00 dB | +0,13 dB |
| 128 kbit/s | 33,89 dB | 33,86 dB | +0,03 dB |

**Decisão.** Fica ligado por omissão em estéreo. A opção `-l` desliga-o, para
experiências.

**Porque é que o ganho é tão pequeno?** Duas razões prováveis:
- os nossos ficheiros podem ter L e R com diferenças grandes;
- o codificador aritmético adapta-se a cada canal em separado, por isso já aproveita
  bem um canal com pouca energia, mesmo que seja R.

Um MID/SIDE escolhido **bloco a bloco** (como faz o codec sem perdas do Duarte) podia
ganhar mais. Está no trabalho futuro (secção 13).

### 5.3 A transformada: MDCT

**O que é.** A MDCT (Modified Discrete Cosine Transform) é a transformada usada no
MP3, no AAC, no Vorbis e no Opus. Está em `src/common/mdct.hpp`.

#### 5.3.1 Porquê MDCT e não a DCT "normal" em blocos separados

A abordagem mais simples seria cortar o áudio em blocos de N amostras e aplicar a
DCT-II a cada um (como o JPEG faz em blocos 8×8). O problema é o **efeito de bloco**:
- cada bloco é quantizado independentemente;
- na fronteira entre dois blocos o erro muda de forma brusca;
- isso cria descontinuidades, que se ouvem como estalidos periódicos e também pioram
  o SNR.

A MDCT resolve isto:
- **Blocos sobrepostos em 50 %.** Cada bloco tem 2N amostras e o seguinte começa N
  amostras depois. Cada amostra pertence a dois blocos.
- **Janela.** Antes da transformada, o bloco é multiplicado por uma janela que sobe
  suavemente de 0 e volta a descer. Na reconstrução, dois blocos consecutivos somam-se
  e as transições são suaves.
- **Amostragem crítica.** Apesar da sobreposição, cada bloco de 2N amostras produz só
  **N coeficientes**, por isso há tantos coeficientes como amostras. Não pagamos bits a
  mais pela sobreposição. (Uma DCT normal com sobreposição de 50 % dobraria o número
  de coeficientes.)
- **TDAC (Time-Domain Aliasing Cancellation).** Produzir N números a partir de 2N
  amostras perde informação em cada bloco ("aliasing" no tempo). Mas o erro de um
  bloco é exatamente o simétrico do erro do bloco seguinte e cancelam-se na soma. Sem
  quantização, a reconstrução é **perfeita**, e o `test_mdct` confirma-o (secção 8).

#### 5.3.2 A janela seno

Usamos w[n] = sin(π(n + ½) / 2N), n = 0…2N−1.

Escolhemo-la porque:
- cumpre a condição de Princen-Bradley, w[n]² + w[n+N]² = 1, necessária para o TDAC
  funcionar e para a transformada ser ortonormal;
- é a janela da MDCT mais comum (Vorbis, AAC em blocos longos);
- é simples e tem boa seletividade em frequência.

A alternativa seria a janela KBD do AAC, que tem outro compromisso entre seletividade
e atenuação. Não a testámos (trabalho futuro).

#### 5.3.3 Como se calcula: dobragem + DCT-IV + FFT

A definição direta da MDCT custa O(N²) por bloco:
- para N = 2048 são ~8 milhões de multiplicações por bloco;
- num ficheiro de 30 s há ~1300 blocos por canal;
- dava dezenas de milhares de milhões de operações, demasiado lento.

Usamos o algoritmo rápido, em três passos:

1. **Dobragem.** O bloco janelado de 2N amostras é dividido em quatro quartos (a, b, c,
   d) e "dobrado" num vetor de N valores: u = (−c′ − d, a − b′), onde ′ significa
   "por ordem inversa". Toda a parte "modificada" da MDCT está aqui.
2. **DCT-IV de N pontos.** X[k] = √(2/N) · Σ u[n]·cos(π/N·(n+½)(k+½)). A DCT-IV é a
   sua própria inversa, por isso o mesmo código serve para os dois sentidos.
3. **DCT-IV via FFT de N/2 pontos.**
   - As amostras pares e as ímpares (por ordem inversa) juntam-se num sinal complexo de
     N/2 pontos: u[2n] + i·u[N−1−2n].
   - Multiplica-se por uma rotação, faz-se uma FFT radix-2 e roda-se outra vez.
   - Os coeficientes saem da parte real (índices pares) e da parte imaginária (índices
     ímpares, por ordem inversa).
   - Custo: O(N log N). Para N = 2048, cerca de 10 000 operações complexas por bloco em
     vez de 8 milhões.

A inversa faz o caminho ao contrário: DCT-IV, depois o "desdobramento"
y = (u₂, −u₂′, −u₁′, −u₁), depois a janela, e por fim a soma com o bloco anterior.

As tabelas (janela, rotações, inversão de bits da FFT) são calculadas uma vez no
construtor da classe `Mdct`. Os vetores de trabalho são reutilizados em todos os
blocos, por isso não há alocações de memória no ciclo principal.

**Porque escrevemos a FFT nós próprios e não usámos a FFTW?**
- Evita uma dependência a mais na instalação (o enunciado pede instalação fácil).
- São ~20 linhas.
- O desempenho chega: a descodificação de 30 s de áudio leva ~0,05 s.

#### 5.3.4 Escala ortonormal

O fator √(2/N), junto com a janela seno, torna a MDCT uma transformada ortonormal
(lapped orthogonal transform). O `test_mdct` confirma que a energia dos coeficientes é
igual à do sinal, com erro relativo < 10⁻¹⁴. É isto que permite a secção 4.4: erro nos
coeficientes = erro no áudio.

#### 5.3.5 Coeficientes em `float`

O codificador precisa de guardar os coeficientes de todo o ficheiro, porque a bisseção
(secção 5.6) codifica várias vezes. Guardamo-los em `float` (4 bytes) em vez de
`double` (8 bytes), o que poupa metade da memória: ~10 MB em vez de ~20 MB no sample01.

O `float` tem ~7 algarismos significativos, um erro relativo de ~10⁻⁷. É milhares de
vezes menor do que o erro da quantização, que mesmo a 256 kbit/s é da ordem de 10⁻²
relativamente ao sinal (SNR de 44 dB).

#### 5.3.6 Tamanho do bloco N

N = número de coeficientes por bloco; a janela tem 2N amostras. Há um compromisso:
- **N grande:** melhor resolução em frequência (cada coeficiente cobre uma banda mais
  estreita) e mais compactação de energia em sons estáveis. Mas o erro espalha-se no
  tempo por toda a janela. Num ataque (bateria, piano) aparece ruído *antes* do som, o
  chamado pré-eco.
- **N pequeno:** boa resolução no tempo, mas pior compactação.

Medimos (secção 9). **N = 2048 é o melhor** nos dois débitos testados e ficou como
valor de omissão. A 44,1 kHz, N = 2048 dá uma janela de 4096 amostras (93 ms) e
coeficientes espaçados de 44100/2/2048 ≈ 10,8 Hz.

O MP3 e o AAC usam N = 576 / 1024 e trocam para blocos curtos nos ataques (*block
switching*), por causa do pré-eco que se ouve. Como o nosso critério é o SNR, e não a
perceção, compensa usar blocos maiores.

### 5.4 Quantização

**O que é.** A função `quantize()` em `lossy.hpp`:

```
q = sinal(X) · ⌊ |X| / Δ + z ⌋          (quantização)
X̂ = q · Δ                                (reconstrução, no descodificador)
```

#### 5.4.1 Porquê o mesmo Δ em todos os coeficientes

É a decisão mais importante do codec, e é diferente do que fazem o MP3 e o AAC.
- O MP3 e o AAC usam um **modelo psicoacústico**. Calculam, para cada banda de
  frequência, quanto ruído o ouvido não consegue ouvir (mascaramento) e usam passos
  diferentes por banda: grandes onde o ruído fica escondido, pequenos onde se ouviria.
  Isto otimiza **o que se ouve**.
- O enunciado mede a distorção com o **SNR**, que é o erro quadrático total, sem pesar
  frequências. Para minimizar o erro quadrático total com um dado número de bits, a
  teoria (alocação de bits ótima / *reverse water-filling*) diz que todos os
  coeficientes devem ter **o mesmo erro**. Com uma transformada ortonormal e um bom
  codificador entrópico, isso consegue-se com **o mesmo passo Δ em todos**.
- Intuição: se uma banda tivesse um passo menor, gastaríamos bits a reduzir o erro
  dela. Esses mesmos bits, gastos noutra banda com passo maior, reduziriam mais o erro
  total. O ponto ótimo é quando o "custo marginal" é igual em todo o lado, e isso dá
  passo igual.
- Vantagem prática: o único parâmetro é Δ. Não há tabelas por banda para guardar no
  ficheiro (só Δ, em 4 bytes) e o controlo do débito fica simples (secção 5.6).

Consequência: **é por isto que temos muito melhor SNR do que o MP3/AAC** (secção 10).
Não somos "melhores" do que eles em geral; otimizamos a medida que o enunciado usa, e
eles não.

#### 5.4.2 Zona morta (o parâmetro z)

Com z = 0,5 temos o arredondamento normal ao inteiro mais próximo. Com z < 0,5, os
valores ficam no nível de menor módulo durante mais tempo: o intervalo que vai para 0
fica mais largo (a **zona morta**), e o mesmo acontece em cada limiar.

Porque é que isto ajuda:
- os coeficientes da MDCT têm uma distribuição muito concentrada em 0 (tipo Laplace);
- puxar um valor como 0,55·Δ para 0 em vez de 1 aumenta um pouco o erro, mas poupa
  bastantes bits, porque o 0 é muito mais barato do que o 1 para o codificador
  aritmético;
- com os bits poupados podemos usar um Δ um pouco menor em todo o ficheiro, e no
  balanço total o SNR sobe;
- é uma forma simples de "otimização débito-distorção" da quantização, usada também em
  codecs de vídeo (H.264/HEVC usam desvios deste tipo).

**Medição (secção 9).** z = 0,40 é o melhor nos dois débitos:
- +0,33 dB a 64 kbit/s e +0,24 dB a 128 kbit/s, comparado com o arredondamento normal
  (z = 0,5);
- abaixo de 0,35 começa a piorar, porque zera demasiados coeficientes úteis.

**Reconstrução.** Usamos X̂ = q·Δ, o centro nominal do nível. Uma pequena correção
para zero (porque, dentro de cada intervalo, os valores tendem a estar mais perto de
zero) podia ganhar mais um pouco. Fica como trabalho futuro.

### 5.5 Codificação entrópica: codificador aritmético

Esta parte transforma os inteiros q em bits. Está em `src/common/rangecoder.hpp` (o
codificador) e em `src/common/lossy.hpp` (o modelo, `CoefModel`, `encodeCoef` e
`decodeCoef`).

#### 5.5.1 Porquê não usar o Golomb/Rice que já tínhamos

A primeira ideia era reutilizar o `golomb.hpp` do codec sem perdas. Medimos e
descartámo-la.

O motivo: depois da quantização, a **grande maioria dos coeficientes é zero**.
Medimos com o passo que o codificador escolhe:

| Ficheiro | Débito | Coeficientes = 0 | Aritmético (nosso) | Rice, k ótimo por bloco e canal | Rice ocupa mais |
|---|---|---|---|---|---|
| sample01 | 64 kbit/s | 84,8 % | 234 773 B | 544 205 B | +132 % |
| sample01 | 128 kbit/s | 70,5 % | 469 546 B | 870 768 B | +85 % |
| sample04 | 64 kbit/s | 86,5 % | 106 772 B | 338 014 B | +217 % |
| sample04 | 128 kbit/s | 75,2 % | 213 546 B | 604 026 B | +183 % |
| sample06 | 64 kbit/s | 87,6 % | 192 106 B | 757 064 B | +294 % |
| sample06 | 128 kbit/s | 79,1 % | 384 213 B | 1 486 783 B | +287 % |

Um código de Rice gasta **no mínimo 1 bit por valor** (o 0 final do unário), mesmo que
o valor seja 0:
- com 44 100 × 2 = 88 200 coeficientes por segundo, isso já dá 88 kbit/s só de zeros;
- é mais do que o débito total de 64 kbit/s que queremos atingir.

O codificador aritmético gasta −log₂(p) bits por decisão. Numa zona onde o modelo já
sabe que quase tudo é 0 (p ≈ 0,99), um zero custa ~0,015 bits. Com o mesmo Δ, o Rice
ocupa **1,9× a 3,9×** mais do que o aritmético.

(Medição feita com um programa auxiliar que quantiza com o Δ escolhido pelo
codificador e conta os bits de Rice com o melhor k para cada bloco e canal, mais 5 bits
para guardar o k.)

#### 5.5.2 O codificador aritmético binário (estilo LZMA)

- **Ideia.** O codificador representa toda a sequência de símbolos como um intervalo
  dentro de [0, 1).
  - Cada decisão binária divide o intervalo atual em duas partes, proporcionais às
    probabilidades de 0 e de 1, e ficamos com a parte do bit que saiu.
  - Decisões prováveis quase não encolhem o intervalo e custam quase 0 bits.
  - No fim, escrevem-se os bits necessários para identificar um ponto do intervalo.
- **Implementação.** Um *range coder* com inteiros de 32 bits, igual ao do LZMA
  (7-Zip/xz), escolhido por ser simples, rápido e bem conhecido.
  - `low` (64 bits) e `range` (32 bits). Sempre que `range` fica abaixo de 2²⁴, sai um
    byte (renormalização).
  - O "transporte" (carry), quando uma soma em `low` altera bytes já decididos, é
    tratado com `cache_` e `pending_`. É a parte mais delicada do código.
  - `finish()` escreve 5 bytes no fim para o descodificador ter sempre dados
    suficientes.
- **Probabilidades adaptativas** (`Prob`).
  - Cada decisão tem uma probabilidade de 11 bits (0…2048), que começa em ½.
  - Depois de cada bit, aproxima-se do valor observado em 1/16 da distância (deslocamento
    `RC_ADAPT = 4`).
  - O descodificador faz exatamente as mesmas atualizações, por isso **não é preciso
    guardar tabelas de probabilidades no ficheiro**. O modelo aprende enquanto codifica,
    e também se adapta a mudanças a meio da música.
  - O valor 4 adapta mais depressa do que o 5 do LZMA. **Não o otimizámos com
    experiências**; ver trabalho futuro.
- **Bits diretos** (`encodeDirect`): para bits praticamente aleatórios (o sinal do
  coeficiente e os bits de baixo de valores grandes) usamos probabilidade ½ fixa, sem
  modelo. É mais rápido e não se perde nada.

#### 5.5.3 Como cada coeficiente é escrito (binarização)

O aritmético é binário, por isso cada inteiro q é escrito como uma sequência de
decisões sim/não:

1. **É zero?** Um bit com modelo (`zero[ctx]`). Se for 0, acabou. É o caso de 70–88 %
   dos coeficientes.
2. **Sinal.** Um bit direto: os positivos e os negativos são igualmente prováveis.
3. **Magnitude em unário com modelo:** "é maior que 1?", "é maior que 2?", …, até 14.
   Cada pergunta tem a sua probabilidade (`gt[ctx][j]`). A maioria dos coeficientes
   não-nulos é pequena (1, 2, 3), por isso estas perguntas são baratas.
4. **Escape para |q| ≥ 15:** código de Exp-Golomb para (|q| − 14). O expoente vai em
   unário com modelo (`esc[banda][i]`) e os bits de baixo vão diretos. Isto permite
   valores enormes (com Δ muito pequeno, ou com 24 bits) sem limite prático.

#### 5.5.4 Contextos: o "conhecimento" do modelo

Uma única probabilidade para "é zero?" seria fraca: perto de 100 Hz quase nada é zero,
e perto de 15 kHz quase tudo é. Por isso a probabilidade depende de um **contexto**,
calculado a partir de informação que o descodificador já tem:

- **Zona de frequência (6 zonas, em escala logarítmica).** f = ⌊k·32/N⌋ vai de 0 a
  31, e a zona é 0 para f = 0, 1 para f = 1, 2 para f = 2–3, 3 para 4–7, 4 para 8–15 e
  5 para 16–31. A 44,1 kHz isto dá:

  | Zona | Frequências |
  |---|---|
  | 0 | 0–689 Hz |
  | 1 | 689–1378 Hz |
  | 2 | 1,4–2,8 kHz |
  | 3 | 2,8–5,5 kHz |
  | 4 | 5,5–11 kHz |
  | 5 | 11–22 kHz |

  Como depende de k/N, a divisão é a mesma qualquer que seja o tamanho do bloco.
- **Atividade à volta (8 níveis):**
  `s = 2·|q[k−1]| + |q[k−2]| + |ant[k]| + (|ant[k−1]| + |ant[k+1]|)/2`.
  - q[k−1] e q[k−2] são os dois coeficientes anteriores do mesmo bloco, que já foram
    codificados.
  - ant[·] são os coeficientes do **bloco anterior** do mesmo canal, à mesma frequência
    e às vizinhas.
  - A ideia: o espetro varia devagar, tanto ao longo da frequência como de bloco para
    bloco. Se os vizinhos são grandes, este provavelmente também é.
  - s é reduzido a 8 níveis: 0, 1, 2, 3–4, 5–7, 8–12, 13–20, >20.

No total são 6 × 8 = 48 contextos para "é zero?" e para as perguntas da magnitude. É
um número pequeno de propósito: cada contexto precisa de dados para aprender, e
contextos a mais aprendem devagar (diluição do contexto).

Esta estrutura (contexto pela vizinhança, probabilidades adaptativas) é a mesma ideia
do CABAC do H.264, em versão simplificada.

### 5.6 Controlo do débito: `-q`, `-r` e `-s`

**Problema.** Para comparar com o MP3/AAC a 128 kbit/s, temos de produzir um ficheiro
de 128 kbit/s. Mas o débito depende de Δ de uma forma que não se sabe à partida: varia
de ficheiro para ficheiro (música calma comprime mais do que ruidosa).

**Solução: três modos.**

| Opção | O que faz |
|---|---|
| `-q PASSO` | Usa o Δ dado. É o mais rápido (codifica uma só vez) |
| `-r KBPS` | Procura o maior Δ cujo débito seja **≤ KBPS** (omissão: `-r 128`) |
| `-s DB` | Procura o maior Δ cujo SNR seja **≥ DB** |

**A bisseção.**
- Quando Δ aumenta, o débito desce e o erro sobe (as duas relações são monótonas).
- Procuramos em log₂(Δ), entre −8 e 20 (Δ entre 0,004 e 1 milhão), com 22
  iterações. No fim a precisão é 28/2²² ≈ 7·10⁻⁶ em log₂ Δ, ou seja, Δ fica certo a
  0,0005 %.
- Procurar em log₂ e não no próprio Δ faz sentido porque o débito varia de forma
  aproximadamente logarítmica com Δ (~6 dB, ou ~1 bit por coeficiente útil, por cada
  duplicação).
- Em cada iteração **codificamos mesmo** (sem escrever o ficheiro) e medimos o tamanho
  exato. Não há estimativas: o débito final fica exatamente no alvo. Por isso aparece
  "128.00 kbps" em todos os ficheiros.
- No fim fica o lado que **cumpre** o alvo: no `-r` o Δ maior (débito ≤ alvo), no `-s` o
  Δ menor (SNR ≥ alvo).
- No `-s`, o SNR de cada iteração é calculado nos coeficientes:
  10·log₁₀(energia do sinal / energia do erro nos coeficientes). Isto vale o mesmo que
  no áudio por causa da ortonormalidade (4.4). O valor real final, medido depois de
  arredondar e limitar as amostras, é impresso no fim e difere em centésimas de dB.

**Custo.** A bisseção faz o codificador ser mais lento do que o MP3. A 128 kbit/s
demora 0,51 s em média, contra 0,25 s do MP3. Com `-q` são ~0,2 s no sample01. O
descodificador não é afetado: só lê Δ do cabeçalho.

**Precisão de Δ.** Δ é guardado no ficheiro como `float` de 32 bits. O codificador
usa **o mesmo valor `float`** (`h.step` é `float`) quando quantiza, por isso o
codificador e o descodificador usam exatamente o mesmo Δ.

### 5.7 Formato do ficheiro comprimido

Os números com vários bytes estão em little-endian.

| Campo | Bytes | Conteúdo |
|---|---|---|
| Assinatura | 4 | "ICY1", para detetar ficheiros errados |
| Taxa de amostragem | 4 | ex.: 44100 |
| Canais | 1 | 1, 2, … |
| Bits por amostra | 1 | 8, 16, 24 ou 32 |
| Formato libsndfile | 4 | para reescrever o WAV no mesmo formato |
| N.º de frames | 8 | para saber onde acabar (o último bloco tem enchimento) |
| N | 2 | tamanho do bloco MDCT |
| Flags | 1 | bit 0 = MID/SIDE |
| Δ | 4 | passo de quantização (float32) |
| **Total do cabeçalho** | **29** | |
| Dados | resto | saída do codificador aritmético |

Ordem dos dados: bloco 0 (canal 0, canal 1), depois bloco 1 (canal 0, canal 1), e
assim por diante. Esta ordem permite ao descodificador reconstruir o áudio à medida
que lê (5.9).

**Porque não usámos o `BitWriter`.** O codificador aritmético produz bytes inteiros,
por isso basta um vetor de bytes. O cabeçalho é escrito byte a byte com `putLE`. O
ficheiro é lido de uma vez com `readFile`, que tem uma linha.

**Robustez:**
- assinatura errada dá "não é um ficheiro do wav_lossy_enc";
- ficheiro com menos de 29 bytes dá "ficheiro comprimido demasiado curto";
- ficheiro cortado a meio dá "RangeDecoder: fim de dados inesperado".

Nos três casos o programa sai com o código 1, sem rebentar. Testado (secção 8).

### 5.8 Início e fim do ficheiro e casos especiais

**Blocos e enchimento.**
- O bloco b cobre as amostras [(b−1)·N, (b+1)·N). As amostras fora do ficheiro valem 0.
- Para cada amostra ser coberta por dois blocos (condição do TDAC), são precisos
  B = ⌈frames/N⌉ + 1 blocos: o primeiro começa N amostras antes do início e o último
  passa o fim.
- O descodificador sabe o número exato de frames pelo cabeçalho e descarta o resto.
  O WAV de saída tem **exatamente** o mesmo número de frames do original, o que é
  necessário para o `wav_cmp`.

**Arredondamento e limites.**
- A saída da MDCT inversa é real. No fim arredonda-se ao inteiro e limita-se ao
  intervalo válido: −32768…32767 em 16 bits; em geral −2^(b−1)…2^(b−1)−1.
- Sem limitar, um pico perto do máximo, somado ao erro de quantização, podia
  ultrapassar o limite e dar a volta (overflow), o que produz um estalido enorme.

**Outros formatos e casos testados:**

| Caso | Resultado |
|---|---|
| Mono 16 bits | SNR 40,1 dB com `-s 40`; o MID/SIDE desliga-se sozinho |
| Estéreo 8 bits | Funciona; o SNR fica limitado pelos 8 bits do próprio formato |
| Estéreo 24 bits | SNR 40,4 dB com `-s 40` |
| Ficheiro com 1 frame | Funciona |
| Ficheiro com 100 frames (menos do que um bloco) | Funciona |
| Silêncio | 103 bytes, SNR = ∞ (sem erro) |

**Silêncio na síntese.** Se todos os coeficientes de um bloco forem 0, salta-se a MDCT
inversa. Poupa tempo em silêncios e em canais S quase nulos.

### 5.9 Memória: síntese bloco a bloco

**O que tínhamos na 1.ª versão.**
- O codificador convertia o ficheiro inteiro para `double` em memória (com MID/SIDE
  e enchimento) antes da MDCT.
- No fim fazia a síntese (para medir o SNR) para outro vetor inteiro de `double`.
- Também guardava todos os coeficientes quantizados.
- Memória máxima medida: **~50 MiB** em média, contra 16 MiB do ffmpeg.

**O que fizemos.**
- `analyze()` passou a ler as amostras diretamente do WAV, bloco a bloco: só existe um
  vetor de 2N amostras de cada vez. Deixou de haver cópia do ficheiro em `double`.
- A síntese passou a ser a classe `Synth`, que recebe um bloco de cada vez:
  - faz a MDCT inversa;
  - soma a primeira metade com a "cauda" guardada do bloco anterior e guarda a segunda
    metade como nova cauda;
  - entrega logo as N frames completas, já em L/R, arredondadas e limitadas, a uma
    função `emit`.
- No **descodificador**, `emit` escreve as frames no WAV de saída. Os coeficientes são
  descodificados um bloco de cada vez e não ficam guardados.
- No **codificador**, `emit` só soma o erro com `Stats` (`metrics.hpp`) para calcular o
  SNR final, sem guardar o áudio reconstruído.
- O codificador e o descodificador usam **a mesma** classe `Synth`, por isso o SNR que o
  codificador anuncia é o que o descodificador vai produzir.

**Resultado** (médias dos 7 ficheiros):

| | 1.ª versão | Versão final |
|---|---|---|
| Codificador | ~50 MiB | 21,8 MiB (29 MiB no sample01, o maior) |
| Descodificador | (não medido) | 18,5–18,9 MiB |

O resultado do codec ficou **igual** (mesmos bytes e mesmo SNR), só mudou a memória.

**O que ainda ocupa memória.**
- Codificador: o WAV de entrada em inteiros de 32 bits (~10 MB no sample01) e os
  coeficientes em `float` (~10 MB). Os coeficientes são necessários por causa da
  bisseção.
- Descodificador: o WAV de saída inteiro (`WavData`) e a cópia que `writeWav` faz para
  converter para o formato da libsndfile.
- O MP3/AAC (ffmpeg) gasta 15–17 MiB. Ver trabalho futuro.

---

## 6. Ficheiros criados ou alterados

| Ficheiro | Novo/alterado | Para que serve |
|---|---|---|
| `src/common/metrics.hpp` | novo | MSE, L∞ e SNR (`Stats`), partilhados pelo `wav_cmp` e pelo codificador |
| `src/wav_cmp.cpp` | alterado | Passou a usar `metrics.hpp`; o resultado ficou igual |
| `src/common/mdct.hpp` | novo | Classe `Mdct`: janela seno, MDCT direta e inversa, DCT-IV via FFT |
| `src/test_mdct.cpp` | novo | Testa a MDCT: igual à definição, conserva a energia, reconstrução perfeita |
| `src/common/rangecoder.hpp` | novo | Codificador e descodificador aritmético binário adaptativo |
| `src/common/lossy.hpp` | novo | Cabeçalho, análise (`analyze`), síntese (`Synth`), `quantize`, modelo de contexto (`CoefModel`), `encodeCoef`/`decodeCoef` |
| `src/wav_lossy_enc.cpp` | novo | Codificador: opções, bisseção de Δ, escrita, SNR final |
| `src/wav_lossy_dec.cpp` | novo | Descodificador bloco a bloco |
| `scripts/experiments_lossy.sh` | novo | Experiências: varia N, z e MID/SIDE com débito fixo |
| `scripts/benchmark_lossy.sh` | novo | Comparação com MP3 e AAC: taxa, SNR, tempos e memória |
| `scripts/plot_rd.py` | novo | Gráfico SNR vs. débito |
| `docs/resultados/experiments_lossy.csv` | novo | Resultados das experiências (por ficheiro) |
| `docs/resultados/benchmark_lossy.csv` | novo | Resultados da comparação (por ficheiro e débito) |
| `docs/relatorio/figuras/rd_lossy.png` | novo | Figura para o relatório |
| `README.md` | alterado | Estado, estrutura, secção 5.6 (uso do codec) e resultados |
| `docs/ia_log.md` | alterado | Entrada sobre o uso de IA nesta parte (falta completar) |
| `docs/parte3_codec_com_perdas.md` | novo | Este documento |

Não foi preciso alterar o `CMakeLists.txt`: cada `src/*.cpp` vira um programa
automaticamente.

---

## 7. Manual de utilização

### 7.1 Compilar

```bash
cmake -B build && cmake --build build
```

### 7.2 Codificar

```bash
./build/wav_lossy_enc [opções] input.wav comprimido
```

| Opção | Significado | Omissão |
|---|---|---|
| `-r KBPS` | Débito alvo em kbit/s | 128 |
| `-s DB` | SNR alvo em dB (em vez de `-r`) | — |
| `-q PASSO` | Passo de quantização fixo (em vez de `-r`/`-s`) | — |
| `-n N` | Coeficientes por bloco MDCT (potência de 2, 16–32768) | 2048 |
| `-z R` | Arredondamento da quantização, 0 < R ≤ 0,5 | 0,40 |
| `-l` | Codificar L/R em vez de MID/SIDE | MID/SIDE em estéreo |

Exemplo:

```
$ ./build/wav_lossy_enc -r 128 data/sample01.wav /tmp/s1.icy
data/sample01.wav -> /tmp/s1.icy: N=2048 MID/SIDE passo=767.559  469546 bytes
128.00 kbps  taxa 11.0:1  SNR 26.27 dB  (0.68 s)
```

O que se lê nesta linha:
- o bloco e o modo de canais usados (`N=2048 MID/SIDE`);
- o Δ encontrado (`passo=767.559`);
- o tamanho, o débito e a taxa de compressão;
- o SNR **real** do áudio que o descodificador vai produzir (média sobre todos os
  canais);
- o tempo de codificação.

### 7.3 Descodificar e medir

```bash
./build/wav_lossy_dec /tmp/s1.icy /tmp/s1.wav
./build/wav_cmp data/sample01.wav /tmp/s1.wav
```

O `wav_cmp` mostra o SNR por canal (L, R) e do MID. O SNR do codificador é calculado
sobre todos os canais juntos, por isso pode diferir umas centésimas da média dos
valores de L e R.

### 7.4 Ouvir

```bash
aplay /tmp/s1.wav
./build/wav_lossy_enc -r 32 data/sample01.wav /tmp/m.icy && ./build/wav_lossy_dec /tmp/m.icy /tmp/m.wav && aplay /tmp/m.wav
```

A 32 kbit/s nota-se a perda de agudos e um "chiar" metálico. É o efeito de zerar a
maior parte dos coeficientes altos, ao contrário do MP3, que esconde o ruído onde não
se ouve. Vale a pena ouvir antes da discussão.

---

## 8. Testes e verificação

| Teste | Como | Resultado |
|---|---|---|
| MDCT rápida = definição | `test_mdct`: DCT-IV rápida vs. fórmula direta O(N²), com N = 16, 64, 256, 1024 e 2048, valores aleatórios | Erro máximo 5,6·10⁻¹⁰ (arredondamento de `double`) |
| Conservação da energia | `test_mdct`: Σx² vs. Σ coeficientes² | Erro relativo ≤ 1,4·10⁻¹⁴ |
| Reconstrução perfeita (TDAC) | `test_mdct`: 8 blocos, MDCT + inversa + sobreposição, sem quantização | Erro máximo 1,3·10⁻¹² |
| Codificador ↔ descodificador | SNR anunciado pelo codificador vs. `wav_cmp` no WAV descodificado, nos 7 ficheiros | Coincidem (ex.: 23,43 vs. 23,46 dB; a diferença vem da média por canal, 7.3) |
| Débito exato | `-r 32…256` nos 7 ficheiros | Sempre exatamente o débito pedido (ex.: 128,0 kbit/s) |
| Casos-limite | Ficheiros gerados com Python: mono, 8 bits, 24 bits, 1 frame, 100 frames, silêncio | Todos funcionam (tabela na 5.8) |
| Ficheiros corrompidos | Ficheiro cortado aos 20 000 bytes e aos 10 bytes | Mensagem de erro e código de saída 1, sem crash |
| Nada se partiu | Compilação limpa (0 avisos com `-Wall -Wextra`); `test_midside`, `test_bitstream`, `test_golomb`; codec sem perdas nos 7 ficheiros | Tudo OK; o codec sem perdas continua igual byte a byte |
| `wav_cmp` igual | Mesmo par de ficheiros antes e depois de passar para `metrics.hpp` | Saída igual |

Para correr:

```bash
./build/test_mdct          # deve terminar com "OK: mdct"
```

---

## 9. Experiências: justificação dos parâmetros

**Método.**
- O débito fica fixo (64 e 128 kbit/s, com `-r`) e varia-se **um parâmetro de cada
  vez**, mantendo os outros nos valores de omissão.
- Mede-se o SNR médio nos 7 ficheiros. Com débito fixo, mais SNR significa melhor
  relação débito-distorção.
- Script: `scripts/experiments_lossy.sh`. Dados por ficheiro em
  `docs/resultados/experiments_lossy.csv`.

### 9.1 Tamanho do bloco N

| N | Janela (ms) | SNR a 64 kbit/s | SNR a 128 kbit/s |
|---|---|---|---|
| 256 | 11,6 | 23,43 | 32,78 |
| 512 | 23,2 | 24,20 | 33,38 |
| 1024 | 46,4 | 24,84 | 33,81 |
| **2048** | **92,9** | **25,13** | **33,89** |
| 4096 | 185,8 | 24,98 | 33,57 |
| 8192 | 371,5 | 24,52 | 32,95 |

**Leitura.**
- Até 2048, aumentar o bloco melhora a compactação de energia (melhor resolução em
  frequência).
- A partir daí, o sinal deixa de ser estável dentro de uma janela tão longa (notas
  mudam, há ataques), e a energia espalha-se por mais coeficientes.
- O máximo está em **N = 2048**, nos dois débitos.
- A diferença entre 256 e 2048 é de **1,7 dB** a 64 kbit/s, um ganho grande.

### 9.2 Arredondamento / zona morta z

| z | SNR a 64 kbit/s | SNR a 128 kbit/s |
|---|---|---|
| 0,50 (arredondamento normal) | 24,80 | 33,65 |
| 0,45 | 25,07 | 33,87 |
| **0,40** | **25,13** | **33,89** |
| 0,35 | 25,06 | 33,76 |
| 0,30 | 24,88 | 33,53 |
| 0,25 | 24,66 | 33,25 |
| 0,20 | 24,40 | 32,94 |

**Leitura.**
- A zona morta ajuda até z = 0,40: +0,33 dB e +0,24 dB face ao arredondamento normal.
- Abaixo disso, zeramos coeficientes que valia a pena manter.
- A curva é suave e tem o máximo no mesmo ponto nos dois débitos, o que dá confiança de
  que não é um acaso destes ficheiros.

### 9.3 MID/SIDE

| Modo | SNR a 64 kbit/s | SNR a 128 kbit/s |
|---|---|---|
| **MID/SIDE** | **25,13** | **33,89** |
| L/R | 25,00 | 33,86 |

**Leitura:** ganho pequeno mas sempre positivo (explicação na 5.2).

### 9.4 Cuidado contra a especialização

O enunciado avisa que o codec vai ser testado com dados privados. As escolhas
generalizam bem:
- N = 2048 e z = 0,40 são valores na zona plana das curvas;
- coincidem com a prática comum (blocos longos de 1024–2048 no AAC e Vorbis; desvios de
  arredondamento entre 1/3 e 1/2 em codecs de vídeo);
- o modelo de contexto é adaptativo e aprende cada ficheiro desde o início, não traz
  tabelas fixas treinadas nos nossos ficheiros.

---

## 10. Comparação com MP3 e AAC

### 10.1 Método

- **Codecs.**
  - MP3 com o `libmp3lame` (LAME), o codificador de MP3 de referência, em CBR com
    `-b:a`.
  - AAC com o codificador nativo do ffmpeg 7.0.2, num contentor `.m4a`, também com
    `-b:a`.
- **Débitos:** 32, 64, 96, 128, 192 e 256 kbit/s, nos 7 ficheiros.
- **Medições** (`scripts/benchmark_lossy.sh`):
  - tamanho, débito real e taxa de compressão;
  - SNR: média de L e R, medida com o nosso `wav_cmp`;
  - tempo e memória máxima (RSS) de compressão e de descompressão, com
    `/usr/bin/time -f "%e %M"`.
- **Mesmo número de amostras.**
  - Os descodificadores do ffmpeg já retiram o atraso inicial do MP3/AAC.
  - Para o `wav_cmp` poder comparar, o número de amostras tem de ser igual ao original:
    o filtro `apad=whole_len=N,atrim=end_sample=N` corta ou completa até N frames.
- **Alinhamento verificado.** Se o MP3 descodificado estivesse desfasado do original,
  nem que fosse uma amostra, o SNR seria injustamente baixo. Confirmámos por correlação
  cruzada (−3000…+3000 amostras) no sample01 que o atraso ótimo é **0** para o MP3 e
  para o AAC.

### 10.2 Resultados médios (7 ficheiros)

| kbit/s | Codec | Débito real | Taxa | SNR (dB) | Compressão (s) | Descompressão (s) | Memória comp./desc. (MiB) |
|---|---|---|---|---|---|---|---|
| 32 | **Nosso** | 32,0 | 44,1 | **18,31** | 0,30 | 0,04 | 21,9 / 18,5 |
| 32 | MP3 | 32,2 | 43,9 | 10,49 | 0,17 | 0,01 | 16,1 / 14,9 |
| 32 | AAC | 33,1 | 42,6 | 7,64 | 0,32 | 0,01 | 16,6 / 15,8 |
| 64 | **Nosso** | 64,0 | 22,1 | **25,11** | 0,38 | 0,04 | 21,8 / 18,6 |
| 64 | MP3 | 64,2 | 22,0 | 15,35 | 0,22 | 0,02 | 16,1 / 14,9 |
| 64 | AAC | 65,5 | 21,6 | 11,13 | 0,33 | 0,01 | 16,7 / 15,8 |
| 96 | **Nosso** | 96,0 | 14,7 | **29,97** | 0,46 | 0,04 | 21,8 / 18,6 |
| 96 | MP3 | 96,3 | 14,6 | 17,75 | 0,24 | 0,02 | 16,1 / 14,8 |
| 96 | AAC | 97,2 | 14,5 | 14,89 | 0,33 | 0,01 | 16,7 / 15,8 |
| 128 | **Nosso** | 128,0 | 11,0 | **33,86** | 0,51 | 0,04 | 21,9 / 18,7 |
| 128 | MP3 | 128,4 | 11,0 | 19,72 | 0,25 | 0,02 | 16,1 / 14,9 |
| 128 | AAC | 129,8 | 10,9 | 18,77 | 0,33 | 0,01 | 16,9 / 15,8 |
| 192 | **Nosso** | 192,0 | 7,4 | **39,65** | 0,66 | 0,05 | 21,9 / 18,9 |
| 192 | MP3 | 192,7 | 7,3 | 25,45 | 0,24 | 0,02 | 16,1 / 14,7 |
| 192 | AAC | 194,2 | 7,3 | 24,26 | 0,36 | 0,01 | 17,0 / 15,8 |
| 256 | **Nosso** | 256,0 | 5,5 | **44,44** | 0,79 | 0,06 | 21,8 / 18,9 |
| 256 | MP3 | 256,9 | 5,5 | 32,67 | 0,25 | 0,02 | 16,2 / 14,8 |
| 256 | AAC | 257,2 | 5,5 | 28,58 | 0,42 | 0,02 | 16,8 / 15,8 |

Figura: `docs/relatorio/figuras/rd_lossy.png`. Em todos os débitos, a nossa curva fica
8–14 dB acima do MP3 e 11–16 dB acima do AAC.

### 10.3 Por ficheiro, a 128 kbit/s

| Ficheiro | Nosso | MP3 | AAC |
|---|---|---|---|
| sample01 | 26,30 | 18,65 | 16,66 |
| sample02 | 28,54 | 17,12 | 15,75 |
| sample03 | 29,32 | 19,11 | 17,16 |
| sample04 | 36,51 | 20,65 | 24,34 |
| sample05 | 41,13 | 21,97 | 19,76 |
| sample06 | 49,79 | 23,08 | 21,65 |
| sample07 | 25,40 | 17,47 | 16,10 |

Os ficheiros "fáceis" (sample05 e sample06) dão SNR muito alto: provavelmente têm pouca
energia nas altas frequências ou são mais tonais. Os "difíceis" (sample01 e sample07)
dão menos. O sample07 está saturado (visto nos histogramas da Parte I), o que costuma
espalhar energia por todo o espetro.

### 10.4 Interpretação: isto quer dizer que somos melhores que o MP3?

**Em SNR, sim, claramente. Em qualidade percebida, não necessariamente.** Isto tem de
ficar claro no relatório e na discussão.
- O MP3 e o AAC usam um modelo psicoacústico. Deixam, de propósito, muito ruído nas
  frequências e nos instantes em que o ouvido não o deteta (mascaramento), e cortam os
  agudos a débitos baixos. Esse ruído "inaudível" conta todo para o SNR.
- O nosso codec minimiza o erro quadrático, que é exatamente o que o SNR mede. Não
  distingue ruído audível de inaudível.
- Por isso a comparação de SNR favorece-nos **por construção**. Num teste de audição a
  débitos baixos (32–64 kbit/s), o MP3/AAC provavelmente soariam melhor.
- Os débitos de MP3/AAC ficam ligeiramente acima do pedido (AAC: +3,4 % em média a
  32 kbit/s; até +4,8 % num ficheiro, o sample07 a 128 kbit/s), o que até os favorece um
  pouco. O nosso fica exatamente no alvo.

**Tempo:**
- codificação 1,5–3× mais lenta do que o MP3, por causa da bisseção (5.6). Com `-q`
  seria comparável;
- descodificação mais lenta em termos relativos (0,04 s contra 0,01–0,02 s), mas
  irrelevante em termos absolutos: ~500× mais rápido do que o tempo real.

**Memória:** gastamos ~22/19 MiB contra ~16/15 MiB. Já baixou de 50 MiB (5.9), e
pode-se reduzir mais (13).

---

## 11. Problemas encontrados e como foram resolvidos

| # | Problema | Como apareceu | Solução |
|---|---|---|---|
| 1 | **Decimais lidos mal pelo `awk`** | Na 1.ª ronda de experiências, os SNR médios saíam como "24,00", "33,00", sempre redondos. O sistema está em português, com vírgula decimal, e o `awk` lia "24.59" como 24 | `export LC_ALL=C` em todos os scripts. Repetimos as experiências; os números da secção 9 são os corretos |
| 2 | **Não havia `ffmpeg` e não tínhamos `sudo`** na máquina usada | `which ffmpeg` vazio; `sudo` pedia password | Usámos o pacote Python `imageio-ffmpeg`, que traz um `ffmpeg` estático (7.0.2, com libmp3lame e AAC), numa pasta temporária. O script aceita `FFMPEG=/caminho`; numa máquina normal basta `sudo apt install ffmpeg` |
| 3 | **MP3/AAC com mais ou menos amostras** do que o original | O MP3 junta enchimento no fim; o `wav_cmp` recusa ficheiros com número de frames diferente | Filtro `apad` + `atrim` do ffmpeg para ficar com exatamente N frames |
| 4 | **Risco de desalinhamento** no MP3/AAC | Se o atraso do codificador não fosse retirado, o SNR do MP3 seria injustamente baixo | Verificação por correlação cruzada: atraso ótimo 0, SNR igual com e sem procura de atraso (18,40 / 16,66 dB no troço testado) |
| 5 | **Memória alta** (~50 MiB) | 1.º benchmark | Análise e síntese bloco a bloco (5.9): 21,8 / 18,7 MiB, com o mesmo resultado |
| 6 | **Rice demasiado caro** nos coeficientes | Análise da distribuição: 70–88 % de zeros | Codificador aritmético com contexto (5.5); medição: Rice ocuparia 1,9–3,9× mais |
| 7 | **Débito ligeiramente acima do alvo** se a bisseção ficasse com o ponto do meio | Lógica da bisseção | No fim fica o lado que cumpre o alvo: Δ maior para `-r`, Δ menor para `-s` |
| 8 | **Codificador e descodificador com Δ diferentes** (risco) | Δ é calculado em `double` mas guardado em `float` | O codificador guarda Δ em `float` (`h.step`) antes de quantizar, por isso os dois usam o mesmo valor |
| 9 | **Saturação** nos picos | Possível em sinais perto do máximo (o sample07 está saturado) | `clip()` na síntese limita ao intervalo do formato antes de converter para inteiro |
| 10 | **SNR do codificador ≠ média do `wav_cmp`** | Diferenças de centésimas (ex.: 23,43 vs. 23,46) | Não é um erro: o codificador calcula o SNR com todos os canais juntos, e a nossa média é de dois SNR separados em dB. Explicado em 7.3 |

---

## 12. Evolução: primeira versão vs. versão final

| Aspeto | 1.ª versão funcional | Versão final | Porque mudou |
|---|---|---|---|
| Bloco N | 1024 | **2048** | Experiência 9.1: +0,29 dB a 64 kbit/s e +0,08 dB a 128 kbit/s |
| Arredondamento z | 0,30 | **0,40** | Experiência 9.2: +0,25 dB (64) e +0,36 dB (128) face a 0,30 com N = 2048 |
| SNR médio a 64 kbit/s | 24,59 dB | **25,13 dB** | Os dois ajustes acima (+0,54 dB) |
| SNR médio a 128 kbit/s | 33,45 dB | **33,89 dB** | (+0,44 dB) |
| Memória do codificador | ~50 MiB | **21,8 MiB** | Síntese e análise bloco a bloco (5.9) |
| Síntese | Ficheiro inteiro em `double`, depois converter | Classe `Synth` com `emit`, partilhada pelo codificador e pelo descodificador | Memória, e garantir que o codificador e o descodificador fazem exatamente o mesmo |
| SNR no codificador | Reconstruía o WAV inteiro e comparava | Compara à medida que sintetiza, com `Stats` | Memória |

O que **não** mudou desde o início, porque funcionou à primeira:
- a MDCT, que passou no `test_mdct` logo na primeira versão;
- o codificador aritmético;
- o formato do ficheiro.

---

## 13. Limitações e trabalho futuro

Por ordem de ganho esperado:

1. **MID/SIDE escolhido bloco a bloco.** Medir o custo nos dois modos para cada bloco e
   escolher o melhor (1 bit por bloco), como faz o codec sem perdas. Hoje é por
   ficheiro.
2. **Reconstrução com correção para zero.** X̂ = sinal(q)·(|q| − δ)·Δ, com δ ≈ 0,1–0,2,
   ou estimado por ficheiro e guardado no cabeçalho. Ganho típico de décimas de dB.
3. **Velocidade de adaptação das probabilidades** (`RC_ADAPT`) e **número de
   contextos.** Não foram afinados com experiências. Também se pode misturar dois
   modelos com velocidades diferentes.
4. **Quantização com otimização débito-distorção** (trellis / RDOQ). Em vez de uma regra
   fixa (z), escolher para cada coeficiente o nível que minimiza erro + λ·bits.
5. **Troca de bloco (block switching) nos ataques.** Para o SNR o ganho é pequeno, mas
   reduz o pré-eco que se ouve.
6. **Janela KBD** em vez de seno: testar.
7. **Codificação mais rápida.**
   - A bisseção pode começar perto do Δ certo, estimado por uma fórmula, e precisar de
     menos iterações.
   - Pode estimar o débito numa amostra dos blocos e só no fim codificar tudo.
8. **Menos memória.**
   - O descodificador pode escrever o WAV por partes com libsndfile, em vez de guardar
     o `WavData` inteiro.
   - O codificador podia ler o WAV por partes se usássemos só `-q`; com `-r`/`-s`
     precisa dos coeficientes todos.
9. **Modo perceptual opcional.** Pesar o passo por banda com um modelo psicoacústico
   simples. Pioraria o SNR mas melhoraria o som. Fica fora do objetivo do enunciado.

---

## 14. Perguntas prováveis na discussão

**"Porque é que o vosso SNR é tão superior ao do MP3?"**
Porque otimizamos o erro quadrático (o mesmo Δ em todos os coeficientes de uma
transformada ortonormal) e o MP3 otimiza a perceção, deixando de propósito ruído
inaudível que conta para o SNR. Ver 5.4.1 e 10.4.

**"Onde está a DCT?"**
A MDCT é calculada como uma DCT-IV de N pontos sobre o bloco "dobrado" (5.3.3). É a
mesma família de transformadas de cosseno, e a variante que todos os codecs de áudio
usam.

**"Porque não usaram a DCT-II em blocos?"**
Por causa do efeito de bloco: descontinuidades nas fronteiras, que se ouvem e baixam o
SNR. A MDCT tem sobreposição sem custo em coeficientes (amostragem crítica e TDAC).

**"Como garantem que a MDCT está certa?"**
Com o `test_mdct`: comparação com a fórmula direta, conservação da energia e
reconstrução perfeita sem quantização, com erros da ordem de 10⁻¹⁰ a 10⁻¹⁴.

**"Porque não usaram o Golomb que já tinham?"**
Porque 70–88 % dos coeficientes são 0 e o Rice gasta pelo menos 1 bit por valor.
Medido: o Rice ocupava 1,9× a 3,9× mais. Ver 5.5.1.

**"Como funciona o codificador aritmético e porque é adaptativo?"**
Ver 5.5.2. Adaptativo porque o descodificador aprende as mesmas probabilidades ao
mesmo tempo, sem tabelas no ficheiro, e acompanha as mudanças da música.

**"O que é o contexto?"**
A zona de frequência e o tamanho dos coeficientes vizinhos, no mesmo bloco e no
anterior, que o descodificador já conhece. Ver 5.5.4.

**"Como conseguem exatamente 128 kbit/s?"**
Por bisseção em log₂ Δ, codificando de verdade em cada iteração. Ver 5.6.

**"Porque N = 2048 e z = 0,4?"**
Por experiência (9.1 e 9.2): são o máximo da curva nos dois débitos e estão numa zona
plana.

**"O que é a zona morta?"**
Ver 5.4.2: troca um pouco de erro por muitos bits nos coeficientes pequenos, e o
balanço é positivo.

**"Porque √2 no MID/SIDE e não /2 como no codec sem perdas?"**
O codec sem perdas precisa de inteiros exatamente reversíveis. O codec com perdas
precisa de ortonormalidade para o erro se conservar. Ver 5.2.

**"E a memória e o tempo?"**
Ver 10.4 e 5.9: memória de 50 → 22 MiB; descodificação ~500× mais rápida do que o tempo
real; a codificação é mais lenta por causa da bisseção.

**"Funciona com mono, 24 bits, ficheiros minúsculos?"**
Sim, foi testado (5.8).

**"O que fariam a seguir?"**
Ver a secção 13. Primeiros: MID/SIDE por bloco, correção da reconstrução e afinação
do modelo.

---

## 15. Uso de IA nesta parte

**Para o Relatório 2.** A implementação da Parte III foi feita com a ajuda do Claude
(Claude Code), a 12 out 2026. Já existe uma entrada em `docs/ia_log.md`.

**O que a IA fez:**
- propôs a arquitetura (MDCT com FFT, MID/SIDE ortonormal, passo único com zona morta,
  codificação aritmética com contexto, bisseção de Δ);
- escreveu o código, os testes, os scripts e este documento;
- correu as experiências e o benchmark.

**Como foi verificado** (isto deve ir para o relatório):
- `test_mdct` contra a definição direta da transformada;
- SNR do codificador confirmado de forma independente pelo `wav_cmp`;
- casos-limite e ficheiros corrompidos;
- regressão dos programas antigos;
- verificação do alinhamento do MP3/AAC por correlação cruzada;
- medição do custo do Rice contra o aritmético.

**Erro detetado durante o trabalho:** o problema do locale no `awk` (secção 11, n.º 1),
que deu números errados na primeira ronda de experiências. Foi detetado porque os
valores saíam suspeitosamente redondos.

**A fazer pelo Francisco antes da entrega:**
- completar no `ia_log.md` os campos "O que decidimos nós", "O que alterámos" e "O que
  aprendemos";
- **perceber cada secção deste documento**, porque na discussão as perguntas são feitas
  a nós e não à IA. Sugestão: refazer à mão o `test_mdct` com N = 16, ouvir os
  ficheiros a 32 kbit/s e tentar explicar a secção 5 sem ler.

---

## 16. Como reproduzir tudo

```bash
# compilar e testar
cmake -B build && cmake --build build
./build/test_mdct

# um exemplo de ponta a ponta
./build/wav_lossy_enc -r 128 data/sample01.wav /tmp/s1.icy
./build/wav_lossy_dec /tmp/s1.icy /tmp/s1.wav
./build/wav_cmp data/sample01.wav /tmp/s1.wav

# experiências dos parâmetros (~5 min) -> results/experiments_lossy.csv
scripts/experiments_lossy.sh

# comparação com MP3/AAC (~3 min; precisa de ffmpeg) -> results/benchmark_lossy.csv
sudo apt install ffmpeg            # uma vez
scripts/benchmark_lossy.sh
# ou, sem sudo:  FFMPEG=/caminho/para/ffmpeg scripts/benchmark_lossy.sh
# só alguns débitos:  RATES="64 128" scripts/benchmark_lossy.sh

# figura para o relatório
python3 scripts/plot_rd.py         # -> docs/relatorio/figuras/rd_lossy.png

# copiar os resultados para o Git (a pasta results/ é de trabalho)
cp results/benchmark_lossy.csv results/experiments_lossy.csv docs/resultados/
```

Os tempos podem variar de máquina para máquina. Os débitos e os SNR são determinísticos.

---

## 17. Estado no Git e próximos passos

**Estado atual.**
- Todo o trabalho está no branch local **`lossy`**, criado a partir do `origin/lossless`
  (PR #8 do Duarte), porque usa a mesma base.
- **Ainda não houve commit nem push.**

**Próximos passos:**
1. Fazer merge do PR #8 (Duarte) no `main`.
2. Fazer commit no branch `lossy`, push e abrir o PR.
3. Escrever a secção da Parte III do Relatório 1 a partir deste documento:
   - máximo de 12 páginas no total e sem código;
   - figuras: `rd_lossy.png` e talvez um gráfico das experiências N/z;
   - tabelas: 9.1, 9.2 e uma versão curta de 10.2.
4. Completar a entrada do `ia_log.md` e a secção da Parte III no Relatório 2.
5. Corrigir no README a autoria do `wav_effects` (foi o Guilherme).
6. Opcional, se houver tempo antes de 25/10: os pontos 1 e 2 da secção 13.
