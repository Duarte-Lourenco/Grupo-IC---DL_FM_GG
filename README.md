# Information and Coding 2026/27 — Trabalho Prático n.º 1

Grupo: Duarte Lourenço (DL) · Francisco Matos (FM) · Guilherme Gabino (GG)

Ferramentas de áudio, codec sem perdas e codec com perdas baseado na DCT.
As instruções de instalação e execução serão acrescentadas à medida que os programas ficarem prontos.

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
