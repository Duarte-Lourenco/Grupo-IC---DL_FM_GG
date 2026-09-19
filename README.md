# Information and Coding 2026/27 — Trabalho Prático n.º 1

Grupo: Duarte Lourenço (DL) · F. M. (FM) · G. G. (GG)

Ferramentas de áudio, codec sem perdas e codec com perdas baseado na DCT.
## Instalação

```bash
sudo apt install build-essential cmake pkg-config libsndfile1-dev
pip install numpy matplotlib

cmake -B build
cmake --build build        # binários ficam em build/
```

Os ficheiros de áudio de teste vão para `data/` (não entram no Git).

## Programas

### `wav_hist` — histogramas

```bash
./build/wav_hist [-k K] [-o saida.csv] entrada.wav canal
#   canal: 0 (L), 1 (R), ..., mid = (L+R)/2, side = (L-R)/2
#   -k K : bins que agrupam 2^K valores

./build/wav_hist data/sample01.wav mid -o mid.csv
./build/wav_hist -k 4 data/sample01.wav side
```

Gráficos (L, R, MID e SIDE de uma vez):

```bash
python3 scripts/plot_hist.py --wav data/sample01.wav
python3 scripts/plot_hist.py --wav data/sample01.wav -k 8 --log -o hist.png
```

### Testes

```bash
./build/test_midside                     # reconstrução exata de L e R a partir de MID e SIDE
./build/test_midside data/sample01.wav   # o mesmo, sobre todas as amostras de um ficheiro
```

## Código partilhado (`src/common/`)

- `wav_io.hpp` — `readWav` / `writeWav` (PCM inteiro de 8 a 32 bits, via libsndfile).
- `channels.hpp` — `midOf`, `halfSideOf`, `sideOf`, `fromMidSide`.
  **Convenção:** a divisão inteira por 2 é sempre feita com `>> 1` (arredonda para −∞).
  Usar sempre estas funções.

---

## Como trabalhamos

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
5. **Abrir um pull request** no GitHub (botão *Compare & pull request*), com uma descrição curta do que foi feito e de como testar.
6. **Revisão:** outro membro lê o código e testa-o. Se estiver tudo bem, aprova e faz *merge*.
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
- Cada utilização de IA fica registada em `docs/ia_log.md` (data, membro, ferramenta, problema, o que foi proposto, como foi verificado). É a base do Relatório 2.
