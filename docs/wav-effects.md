# Efeitos de áudio — `wav_effects`

O `wav_effects` aplica quatro efeitos a um WAV: eco simples, ecos múltiplos, modulação de
amplitude e atraso variável. Todos se reduzem a três operações sobre as amostras:
**atrasar**, **multiplicar por um ganho** e **somar**.

```bash
./build/wav_effects entrada.wav saida.wav efeito [parâmetros]
```

**Índice:**
[Conceitos base](#conceitos-base) ·
[Eco simples](#eco-simples-echo) ·
[Ecos múltiplos](#ecos-múltiplos-multiecho) ·
[Diagrama de blocos](#diagrama-de-blocos-eco-simples-vs-ecos-múltiplos) ·
[Modulação de amplitude](#modulação-de-amplitude-am) ·
[Atraso variável](#atraso-variável-vdelay) ·
[Resumo](#resumo)

---

## Conceitos base

Um WAV é uma lista de números (amostras): 44100 por segundo e por canal, cada um entre
−32768 e 32767 (16 bits). Um efeito é uma regra que calcula cada amostra de saída y[n] a
partir das amostras de entrada x[n]. Os canais L e R são processados de forma
independente, com a mesma regra.

- **Atraso.** Um atraso em milissegundos converte-se em amostras com
  D = atraso_ms × fs / 1000. Com fs = 44100 Hz, 10 ms são 441 amostras e 300 ms são 13230.
  x[n − D] é a amostra que entrou D amostras antes. Antes do início do ficheiro (índice
  negativo), x vale 0.
- **Ganho.** O ganho a multiplica uma amostra: a = 0.5 reduz a amplitude para metade, ou
  seja, −6 dB.
- **Saturação.** Somar sinais pode ultrapassar 32767. Esses valores são limitados ao
  extremo (*clipping*), o que se ouve como distorção. O programa diz quantas amostras
  saturaram, para se poder baixar o ganho.
- **Contas em `double`.** O resultado só é arredondado para inteiro no fim. Isto importa
  nos ecos múltiplos, onde cada saída volta a entrar no cálculo: arredondar a cada passo
  acumularia erro.
- **Duração.** A saída tem a mesma duração da entrada: a cauda dos ecos depois do fim é
  cortada. Assim, original e resultado podem ser comparados com o `wav_cmp`.

## Eco simples (`echo`)

O eco simples soma ao som uma cópia dele próprio, atrasada D amostras e atenuada pelo
ganho a.

```math
y[n] = x[n] + a \cdot x[n - D]
```

**O que acontece às amostras.** Cada amostra de saída é a amostra atual mais uma fração da
que entrou D amostras antes. Só se usam valores da entrada, por isso este sistema chama-se
não recursivo (FIR). Basta guardar as últimas D amostras.

**Resposta a um impulso.** Com um único "clique" de valor 10000 em n = 0, atraso de 10 ms e
a = 0.5, a saída tem 10000 em n = 0 e 5000 em n = 441. Todas as outras amostras são 0. Ou
seja, há exatamente uma repetição.

**Efeito audível.** Depende do atraso:

- **Mais de 50–100 ms:** o ouvido separa as duas cópias e ouve-se um eco distinto.
- **Menos de ~30 ms:** as cópias fundem-se. O som muda de timbre porque certas frequências
  se anulam (filtro em pente).

**Medida com o `wav_cmp`.** Comparado com o original, o eco com a = 0.5 dá SNR ≈ 6 dB. É o
valor esperado: a diferença entre os ficheiros é 0.5·x[n − D], com um quarto da energia do
sinal, e 10·log₁₀(1/0.25) = 6.02 dB.

```bash
./build/wav_effects in.wav out.wav echo 300 0.5
```

## Ecos múltiplos (`multiecho`)

Os ecos múltiplos somam à entrada a **saída** atrasada, e não a entrada atrasada. Por isso,
cada eco gera outro eco.

```math
y[n] = x[n] + a \cdot y[n - D]
```

**O que acontece às amostras.** A fórmula usa um valor de saída já calculado: é uma
realimentação, e o sistema chama-se recursivo (IIR). Desenvolvendo a equação obtém-se uma
soma infinita de cópias, cada uma mais fraca:

```math
y[n] = x[n] + a\,x[n-D] + a^2\,x[n-2D] + a^3\,x[n-3D] + \dots
```

**Resposta a um impulso.** Com 10000 em n = 0, D = 441 e a = 0.5, a saída tem 10000, 5000,
2500, 1250, 625… em n = 0, 441, 882, 1323, 1764… A amplitude cai para metade a cada
repetição.

**Estabilidade: |a| < 1.** Se |a| ≥ 1, cada eco é tão forte ou mais forte do que o
anterior. O som nunca se extingue e acaba sempre em saturação. O programa recusa esses
valores. Quanto mais perto de 1, mais tempo os ecos duram: com a = 0.6 são precisas ~9
repetições para cair 40 dB (1 % da amplitude), e com a = 0.9 são precisas ~44.

**Efeito audível.**

- **Atraso longo (250 ms):** "eco de montanha", com repetições cada vez mais fracas.
- **Atraso curto (20–40 ms):** as repetições fundem-se numa cauda que soa a uma
  reverberação simples.

```bash
./build/wav_effects in.wav out.wav multiecho 250 0.6
```

## Diagrama de blocos: eco simples vs. ecos múltiplos

Os dois efeitos usam as mesmas peças: atraso, ganho e somador. A única diferença é o ponto
de onde sai a cópia.

**Eco simples (FIR):** a cópia sai da **entrada**, por isso há exatamente uma repetição.

```text
x[n] ──┬──────────────────────────────────► (+) ──► y[n]
       │                                     ▲
       │   ┌──────────┐    ┌───────┐         │
       └──►│ atraso D │───►│  × a  │─────────┘
           └──────────┘    └───────┘
```

**Ecos múltiplos (IIR):** a cópia sai da **saída** (realimentação), por isso cada eco gera
outro (a, a², a³, …). Os ecos só se extinguem se |a| < 1.

```text
x[n] ──► (+) ───────────────────────────────┬──► y[n]
          ▲                                 │
          │  ┌───────┐    ┌──────────┐      │
          └──│  × a  │◄───│ atraso D │◄─────┘
             └───────┘    └──────────┘
```

## Modulação de amplitude (`am`)

A modulação de amplitude multiplica cada amostra por um cosseno de frequência f. Aqui não há
atraso: o volume do som passa a variar ao longo do tempo.

```math
y[n] = x[n] \cdot \cos\left(\frac{2\pi f n}{f_s}\right)
```

**O que acontece às amostras.** O cosseno vai de −1 a 1, por isso cada amostra é escalada
por um fator que muda suavemente. Quando o fator é negativo, a amostra troca de sinal. Como
|cos| ≤ 1, a saída nunca é maior do que a entrada: este efeito não satura. Por exemplo, com
um sinal constante de 10000 e f = 1 Hz, a saída é 10000 em n = 0, 0 em n = 11025 (¼ s) e
−10000 em n = 22050 (½ s).

**Efeito audível.** Depende de f:

- **f baixa (1–10 Hz): tremolo.** O ouvido não distingue o sinal do fator, só o volume, que
  acompanha |cos|. Este tem dois máximos por período, por isso o volume pulsa 2f vezes por
  segundo: com f = 2 Hz ouvem-se 4 pulsações por segundo.
- **f audível (100–1000 Hz): *ring modulation*.** Multiplicar por um cosseno desloca cada
  frequência do som para (frequência − f) e (frequência + f). As harmonias deixam de ser
  múltiplos umas das outras e o som fica metálico, "de robô".

**Limite.** f tem de ser menor do que fs/2 (22050 Hz, a frequência de Nyquist). Acima
disso, o cosseno amostrado é indistinguível de um de frequência mais baixa (*aliasing*).

```bash
./build/wav_effects in.wav out.wav am 5
```

## Atraso variável (`vdelay`)

O atraso variável é um eco simples em que o atraso D(n) muda continuamente, guiado por um
oscilador lento de frequência f (um *LFO*).

```math
y[n] = x[n] + a \cdot x[n - D(n)], \qquad D(n) = \frac{D_{max}}{2}\left(1 - \cos\frac{2\pi f n}{f_s}\right)
```

**O que acontece às amostras.** D(n) oscila suavemente entre 0 e Dmax e começa em 0, por
isso o efeito entra sem salto. Com Dmax = 10 ms e f = 0.5 Hz, o atraso vai de 0 a 441
amostras e volta a 0 a cada 2 segundos. O ganho por omissão é a = 0.7.

**Atraso fracionário e interpolação linear.** Na maior parte do tempo, D(n) não é inteiro:
por exemplo, n − D(n) = 1000.3 cai entre duas amostras. O valor usa-se pesando as duas
vizinhas:

```math
x[1000.3] \approx 0.7\,x[1000] + 0.3\,x[1001]
```

Se o atraso fosse arredondado a inteiros, saltaria de amostra em amostra e ouvir-se-iam
pequenos estalos (*zipper noise*). A interpolação foi verificada com uma rampa, um sinal em
que ela é exata: 0 erros em 28000 amostras.

**Efeito audível.** Combina dois fenómenos:

- **Filtro em pente que desliza.** Somar o som a uma cópia atrasada anula certas
  frequências, que dependem do atraso. Como o atraso muda, essas anulações varrem o
  espectro para cima e para baixo. É o som "de avião a jato" do **flanger** (Dmax até
  ~10 ms).
- **Variação de afinação (efeito Doppler).** Enquanto o atraso cresce, a cópia é lida mais
  devagar e soa mais grave; quando diminui, soa mais aguda. Com 10 ms e 0.5 Hz, a afinação
  da cópia varia cerca de ±1.6 %. Com atrasos de 20–30 ms, a cópia soa como uma segunda voz
  ligeiramente desafinada: é o **chorus**.

```bash
./build/wav_effects in.wav out.wav vdelay 10 0.5 0.9
```

## Resumo

Ecos e atraso variável mexem no **tempo** (somam cópias atrasadas). A modulação de
amplitude mexe no **volume** (multiplica por um fator que varia).

| Efeito | Operação sobre as amostras | Tipo de sistema | Pode saturar? | Som típico |
| --- | --- | --- | --- | --- |
| `echo` | soma 1 cópia atrasada da entrada | FIR (só usa a entrada) | sim, se a for grande | uma repetição |
| `multiecho` | soma a saída atrasada (realimentação) | IIR, estável só se \|a\| < 1 | sim | repetições a decair, reverberação |
| `am` | multiplica por cos(2πfn/fs) | sem memória, varia no tempo | não (\|cos\| ≤ 1) | tremolo ou som metálico |
| `vdelay` | soma 1 cópia com atraso D(n) variável, interpolada | FIR, varia no tempo | sim, se a for grande | flanger, chorus |

Tempo de execução: ~0.04 s para os 29 s do `sample01`, em qualquer dos quatro efeitos. Todos
fazem um número fixo de operações por amostra.
