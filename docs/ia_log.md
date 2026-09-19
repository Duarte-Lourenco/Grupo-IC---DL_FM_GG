# Registo do uso de IA

Base do Relatório 2. Uma entrada por utilização relevante, factual: o relatório tem de
corresponder a este registo. Os campos "O que aprendemos" são preenchidos por quem fez a
tarefa, pelas suas palavras.

**Resumo da forma como usámos IA:** o assistente (Claude, no modo Cowork) serviu de
tutor e guia. Explicou os conceitos (formato WAV, MID/SIDE, divisão inteira, CMake, Git),
propôs uma organização do trabalho e gerou uma primeira versão de parte do código.
As decisões foram do grupo: aceitámos, adaptámos ou rejeitámos as propostas com base
no que aprendemos e no enunciado. Estudámos e verificámos tudo o que integrámos.

Modelo:

```markdown
### AAAA-MM-DD — Nome — Ferramenta
- **Problema:**
- **O que a IA propôs / explicou:**
- **Como verificámos:**
- **Estava correto?**
- **O que decidimos nós:**
- **O que alterámos:**
- **O que aprendemos:**
```

---

### 2026-09-19 — Duarte — Claude (Cowork)
**Tarefa:** leitura do enunciado e escolha das linguagens.
- **Problema:** decidir como organizar o trabalho e que linguagens usar.
- **O que a IA propôs / explicou:** C++ para os programas e codecs, porque o tempo e a memória contam para a avaliação; Python (numpy/matplotlib) para gráficos e benchmarks; libsndfile para ler WAV; CMake para compilar.
- **Como verificámos:** o enunciado diz explicitamente que o tempo de execução e a memória são avaliados.
- **Estava correto?** Sim.
- **O que decidimos nós:** a ideia inicial do Duarte era fazer tudo em Python no VSCode. Depois de comparar as duas opções com o que o enunciado exige, decidimos dividir: C++ para o processamento e Python só para a visualização, o que mantém a vantagem de ver os gráficos no VSCode.
- **O que alterámos:** —
- **O que aprendemos:**

### 2026-09-19 — Duarte — Claude (Cowork)
**Tarefa:** organização do trabalho em grupo e esqueletos dos relatórios.
- **O que a IA propôs / explicou:** a estrutura dos dois relatórios em LaTeX, com base no enunciado; como funciona o fluxo "um branch por tarefa + pull request revisto por outro membro"; uma divisão possível das tarefas por três pessoas.
- **Como verificámos:** compilámos os `.tex` e comparámos a estrutura com os pontos 1–10 do enunciado.
- **O que decidimos nós:**
  - quem fica com cada parte: Duarte (A), Francisco (B) e Guilherme (C);
  - fazer primeiro a base comum e só depois dividir o trabalho;
  - avançar passo a passo, primeiro só os dados e depois só a base;
  - separar a base comum e o `wav_hist` em pull requests diferentes;
  - pedir um README com o plano de cada pessoa e comandos prontos a usar.
- **O que aprendemos:**

### 2026-09-19 — Duarte — Claude (Cowork)
**Tarefa:** base comum (`wav_io.hpp`, `channels.hpp`, `CMakeLists.txt`), `test_midside` e `wav_hist` + `plot_hist.py`.
- **O que a IA propôs / explicou:** gerou a primeira versão destes ficheiros e explicou-os em detalhe:
  - o formato WAV e a razão para usar uma biblioteca comum;
  - a correção da escala da libsndfile (amostras alinhadas a 32 bits);
  - a diferença entre `/` e `>>` nos números negativos;
  - a reconstrução exata L + R = 2·MID + (SIDE & 1).
- **Como verificámos:**
  - pedimos a explicação de cada ficheiro e estudámo-la até a conseguirmos reproduzir;
  - os histogramas foram comparados com `numpy.unique`;
  - o `test_midside` confirmou a reconstrução exata em todas as amostras dos 7 ficheiros;
  - a revisão do pull request pelo Francisco está por fazer.
- **Estava correto?** Sim, confirmado pelos testes.
- **O que decidimos nós:** adotar `>> 1` como a convenção única de divisão inteira para todo o grupo, depois de percebermos o risco de o codificador e o descodificador arredondarem de maneira diferente.
- **O que aprendemos:**

### 2026-09-19 — Duarte — Claude (Cowork)
**Tarefa:** erro na organização do CMake.
- **Problema:** o commit do branch `base-comum` tinha um `CMakeLists.txt` que pedia para compilar o `wav_hist` sem incluir o ficheiro `wav_hist.cpp`, por isso o projeto não compilaria nos computadores dos colegas. A versão corrigida enviada pela IA não tinha chegado a substituir o ficheiro local.
- **Como foi detetado:** pela IA, ao verificar o estado do repositório.
- **O que se alterou:** o CMake passou a compilar automaticamente cada `src/*.cpp`, o que também evita conflitos no Git entre os três.
- **O que aprendemos:** confirmar sempre o que entra num commit com `git status` / `git diff --staged`.

### 2026-09-19 — Duarte — Claude (Cowork)
**Tarefa:** gráficos dos 7 ficheiros e tabela de entropia e desvio padrão (`results/estatisticas.csv`).
- **O que a IA propôs / explicou:** gerou os gráficos e a tabela e explicou o que a entropia representa para a compressão.
- **Observações para o relatório:**
  - o SIDE tem sempre menor desvio padrão;
  - em 5 dos 7 ficheiros, o SIDE também tem menor entropia;
  - o sample02 e o sample06 são exceções;
  - o sample07 está saturado.
- **O que decidimos nós:** que figuras usar no relatório; escrever a secção do relatório nós próprios, com a IA apenas a rever.
- **O que aprendemos:**

### 2026-09-19 — Duarte — Claude (Cowork)
**Tarefa:** abordagem ao Relatório 2.
- **O que decidimos nós:** apresentar a IA como tutor e ferramenta, com as decisões e a verificação do lado do grupo, e registar este uso de forma factual.
