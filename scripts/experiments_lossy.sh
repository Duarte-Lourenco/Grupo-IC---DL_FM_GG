#!/usr/bin/env bash
# experiments_lossy.sh — justificação experimental dos parâmetros do codec com perdas.
#
# Com o débito fixo (-r), varia um parâmetro de cada vez e mede o SNR médio
# nos ficheiros (quanto maior, melhor a relação débito-distorção):
#   N   tamanho do bloco MDCT       (-n)
#   z   arredondamento / zona morta (-z)
#   ms  MID/SIDE ligado ou não      (-l)
#
# Uso:  scripts/experiments_lossy.sh [ficheiros.wav...]    (omisso: data/*.wav)
# Saída: results/experiments_lossy.csv  +  resumo no ecrã
set -u
export LC_ALL=C

ENC=build/wav_lossy_enc
OUT=results/experiments_lossy.csv
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
[ -x "$ENC" ] || { echo "Compila primeiro: cmake --build build"; exit 1; }
mkdir -p results

FILES=("$@")
[ ${#FILES[@]} -eq 0 ] && FILES=(data/*.wav)

echo "parametro,valor,kbps,ficheiro,snr_dB" > "$OUT"

# exp NOME VALOR KBPS opções...
exp() {
  local p=$1 v=$2 r=$3; shift 3
  for f in "${FILES[@]}"; do
    s=$("$ENC" -r "$r" "$@" "$f" "$TMP/x.icy" | grep -o 'SNR [0-9.-]*' | cut -d' ' -f2)
    echo "$p,$v,$r,$(basename "$f" .wav),$s" >> "$OUT"
  done
}

for r in 64 128; do
  for n in 256 512 1024 2048 4096 8192; do exp N "$n" "$r" -n "$n"; done
  for z in 0.50 0.45 0.40 0.35 0.30 0.25 0.20; do exp z "$z" "$r" -z "$z"; done
  exp ms MID/SIDE "$r"
  exp ms L/R "$r" -l
done

echo "Resultados em $OUT"
echo
awk -F, 'NR>1 {k=$1","$2","$3; if (!(k in n)) ord[++m]=k; n[k]++; s[k]+=$5}
END {
  printf "%-4s %-9s %6s %9s\n", "par", "valor", "kbps", "SNR(dB)";
  for (i = 1; i <= m; i++) { split(ord[i], p, ","); printf "%-4s %-9s %6s %9.2f\n", p[1], p[2], p[3], s[ord[i]]/n[ord[i]] }
}' "$OUT"
