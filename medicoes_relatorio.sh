#!/usr/bin/env bash
# medicoes_relatorio.sh — produz as medicoes que faltam para o Relatorio 1.
#
# Correr a partir da raiz do repositorio:
#     bash medicoes_relatorio.sh
#
# Faz: condicoes da maquina, histograma C++ vs Python, varredura de fios de
# execucao, varredura de tamanho de bloco e tabela da quantizacao.
# Nao escreve nada no repositorio; os ficheiros intermedios vao para /tmp.

set -u
REF_NAME="052_mix.wav"
N_REP=5

say() { printf '\n###### %s\n' "$*"; }

say "0. CONDICOES DA MAQUINA"
GOV=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "?")
echo "governor: $GOV"
if [ "$GOV" != "performance" ]; then
  echo
  echo "  !! O governador NAO esta em performance."
  echo "  !! Os tempos medidos assim nao sao comparaveis com o resto do relatorio."
  echo "  !! Corrige com:"
  echo "       sudo cpupower frequency-set -g performance"
  echo "     ou, se o cpupower nao existir:"
  echo "       echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"
  echo
  read -r -p "  Continuar mesmo assim? (s/N) " R
  [ "${R:-n}" = "s" ] || exit 1
fi
lscpu | grep -E "^Model name|^CPU\(s\):" | sed 's/  */ /g'
free -h | awk 'NR==2{print "memoria total: "$2}'
g++ --version | head -1
echo -n "numpy: "; python3 -c "import numpy; print(numpy.__version__)" 2>/dev/null || echo "NAO instalado"

W=$(find data -name "$REF_NAME" 2>/dev/null | head -1)
if [ -z "$W" ]; then
  echo "nao encontrei $REF_NAME dentro de data/ --- ajusta REF_NAME no topo do script"
  exit 1
fi
echo "ficheiro de referencia: $W"

if [ ! -x build/wav_hist ]; then
  say "a compilar"
  cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j >/dev/null
fi

say "1. HISTOGRAMA: C++ vs Python  (Seccao 2.1)"
if [ -f bench_hist.py ]; then
  python3 bench_hist.py "$W" --bin build/wav_hist -n "$N_REP"
else
  echo "bench_hist.py nao esta na raiz do repositorio"
fi

# cronometro portatil: usa /usr/bin/time se existir, senao um involucro em Python
cat > /tmp/_cron.py <<'PYEOF'
import subprocess, resource, time, sys
t0 = time.time()
a = resource.getrusage(resource.RUSAGE_CHILDREN)
subprocess.run(sys.argv[1:], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
b = resource.getrusage(resource.RUSAGE_CHILDREN)
cpu = (b.ru_utime - a.ru_utime) + (b.ru_stime - a.ru_stime)
print("%.2f %.2f %d" % (time.time() - t0, cpu, b.ru_maxrss))
PYEOF
cron() {   # imprime "parede cpu memoria_KiB"
  if [ -x /usr/bin/time ]; then
    /usr/bin/time -f "%e %U %M" "$@" 2>&1 >/dev/null | tail -1
  else
    python3 /tmp/_cron.py "$@"
  fi
}

say "2. FIOS DE EXECUCAO  (Seccao 4.4)"
printf '%-8s %10s %10s %12s\n' "fios" "parede(s)" "CPU(s)" "memoria(KiB)"
for t in 1 2 4 8; do
  best_w=""; best_c=""; best_m=""
  for _ in $(seq "$N_REP"); do
    set -- $(cron ./build/wav_lossless_enc "$W" "/tmp/t_$t.icl" --threads "$t")
    w=${1:-}; c=${2:-}; m=${3:-}
    [ -n "$w" ] || continue
    if [ -z "$best_w" ] || awk "BEGIN{exit !($w < $best_w)}"; then best_w=$w; best_c=$c; best_m=$m; fi
  done
  printf '%-8s %10s %10s %12s\n' "$t" "${best_w:-?}" "${best_c:-?}" "${best_m:-?}"
done
if cmp -s /tmp/t_1.icl /tmp/t_8.icl; then
  echo "saida identica com 1 e 8 fios: SIM"
else
  echo "saida identica com 1 e 8 fios: NAO  <-- investigar, e um bug"
fi

say "3. TAMANHO DE BLOCO, neste ficheiro  (Seccao 4.2)"
ORIG=$(stat -c%s "$W")
printf '%-8s %12s %10s\n' "bloco" "bytes" "taxa"
for b in 2048 4096 8192 16384; do
  ./build/wav_lossless_enc "$W" "/tmp/b_$b.icl" -b "$b" >/dev/null 2>&1
  S=$(stat -c%s "/tmp/b_$b.icl")
  printf '%-8s %12s %10s\n' "$b" "$S" "$(awk "BEGIN{printf \"%.4f\", $ORIG/$S}")"
done

say "4. QUANTIZACAO  (Seccao 3.3)"
echo "ficheiro: $W   (canal L)"
printf '%-6s %-10s %14s %14s %8s %10s\n' "bits" "passo" "MSE teorico" "MSE medido" "Linf" "SNR(dB)"
for b in 12 8 4 2; do
  D=$(( 1 << (16 - b) ))
  TEO=$(awk "BEGIN{printf \"%.1f\", ($D*$D)/12}")
  ./build/wav_quant -b "$b" "$W" "/tmp/q_$b.wav" >/dev/null 2>&1
  LINHA=$(./build/wav_cmp "$W" "/tmp/q_$b.wav" 2>/dev/null | awk '$1=="L"{print}')
  MSE=$(echo "$LINHA" | sed -n 's/.*MSE *= *\([0-9.]*\).*/\1/p')
  LINF=$(echo "$LINHA" | sed -n 's/.*Linf *= *\([0-9]*\).*/\1/p')
  SNR=$(echo "$LINHA" | sed -n 's/.*SNR *= *\([0-9.-]*\).*/\1/p')
  printf '%-6s %-10s %14s %14s %8s %10s\n' "$b" "$D" "$TEO" "${MSE:-?}" "${LINF:-?}" "${SNR:-?}"
done
echo
echo "diferenca de SNR por bit retirado (esperado: 6,02 dB):"
./build/wav_quant -b 8 "$W" /tmp/qa.wav >/dev/null 2>&1
./build/wav_quant -b 7 "$W" /tmp/qb.wav >/dev/null 2>&1
A=$(./build/wav_cmp "$W" /tmp/qa.wav 2>/dev/null | awk '$1=="L"{sub(/.*SNR *= */,""); sub(/ .*/,""); print}')
B=$(./build/wav_cmp "$W" /tmp/qb.wav 2>/dev/null | awk '$1=="L"{sub(/.*SNR *= */,""); sub(/ .*/,""); print}')
awk "BEGIN{printf \"  8 bits: %s dB,  7 bits: %s dB,  diferenca %.2f dB\n\", \"$A\", \"$B\", $A-$B}"

say "5. PONTO MEDIO vs TRUNCAGEM  (Seccao 3.3)"
printf '%-6s %16s %16s\n' "bits" "MSE ponto medio" "MSE truncagem"
for b in 8 4; do
  ./build/wav_quant -b "$b" "$W" /tmp/qm.wav >/dev/null 2>&1
  ./build/wav_quant -b "$b" -t "$W" /tmp/qt.wav >/dev/null 2>&1
  M=$(./build/wav_cmp "$W" /tmp/qm.wav 2>/dev/null | sed -n 's/^L.*MSE *= *\([0-9.]*\).*/\1/p')
  T=$(./build/wav_cmp "$W" /tmp/qt.wav 2>/dev/null | sed -n 's/^L.*MSE *= *\([0-9.]*\).*/\1/p')
  printf '%-6s %16s %16s\n' "$b" "${M:-?}" "${T:-?}"
done

say "FIM"
echo "Cola este output todo no chat."
