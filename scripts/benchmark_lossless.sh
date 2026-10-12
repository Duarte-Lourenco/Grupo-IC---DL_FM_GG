#!/usr/bin/env bash
# benchmark_lossless.sh — compara o nosso codec sem perdas com o estado da arte.
#
# Para cada ficheiro e cada codec mede: tamanho comprimido, taxa de compressão,
# tempo de compressão e de descompressão (s) e memória máxima (MiB), e confirma
# que a descompressão devolve o ficheiro original byte a byte.
#
# Uso:  scripts/benchmark_lossless.sh [ficheiros.wav...]      (omisso: data/*.wav)
# Saída: results/benchmark_lossless.csv  +  tabela resumo no ecrã
set -u

ENC=build/wav_lossless_enc
DEC=build/wav_lossless_dec
OUT=results/benchmark_lossless.csv
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

[ -x "$ENC" ] && [ -x "$DEC" ] || { echo "Compila primeiro: cmake --build build"; exit 1; }
mkdir -p results

FILES=("$@")
[ ${#FILES[@]} -eq 0 ] && FILES=(data/*.wav)

# corre um comando; devolve "tempo_s memoria_MiB"
run() {
  local t
  t=$(/usr/bin/time -f "%e %M" "$@" 2>&1 >/dev/null | tail -1)
  echo "$t" | awk '{printf "%.2f %.1f", $1, $2/1024}'
}

echo "ficheiro,codec,bytes_orig,bytes_comp,taxa,t_comp_s,t_desc_s,mem_comp_MiB,exato" > "$OUT"

for f in "${FILES[@]}"; do
  [ -f "$f" ] || continue
  name=$(basename "$f" .wav)
  orig=$(stat -c%s "$f")

  for codec in nosso flac8 flac5 zstd19 xz9; do
    c="$TMP/$name.$codec"; d="$TMP/$name.$codec.wav"
    case $codec in
      nosso)  ce=$(run $ENC "$f" "$c");            cd=$(run $DEC "$c" "$d") ;;
      flac8)  ce=$(run flac -8 -f -s "$f" -o "$c"); cd=$(run flac -d -f -s "$c" -o "$d") ;;
      flac5)  ce=$(run flac -5 -f -s "$f" -o "$c"); cd=$(run flac -d -f -s "$c" -o "$d") ;;
      zstd19) ce=$(run zstd -19 -q -f "$f" -o "$c"); cd=$(run zstd -d -q -f "$c" -o "$d") ;;
      xz9)    cp "$f" "$TMP/in.wav"; ce=$(run xz -9 -f -k "$TMP/in.wav"); mv "$TMP/in.wav.xz" "$c"
              cp "$c" "$TMP/in2.xz";  cd=$(run xz -d -f "$TMP/in2.xz"); mv "$TMP/in2" "$d" ;;
    esac
    comp=$(stat -c%s "$c")
    cmp -s "$f" "$d" && exato=sim || exato=NAO
    taxa=$(awk -v o="$orig" -v c="$comp" 'BEGIN{printf "%.3f", o/c}')
    echo "$name,$codec,$orig,$comp,$taxa,${ce% *},${cd% *},${ce#* },$exato" >> "$OUT"
    rm -f "$c" "$d"
  done
done

echo
echo "Resultados em $OUT"
echo
{ awk -F, 'NR>1 {n[$2]++; t[$2]+=$5; tc[$2]+=$6; td[$2]+=$7; m[$2]+=$8; if($9!="sim") bad[$2]++}
END {
  printf "%-8s %8s %10s %10s %9s %8s\n","codec","taxa","t_comp(s)","t_desc(s)","mem(MiB)","exato";
  for (c in n) printf "%-8s %8.3f %10.2f %10.2f %9.1f %8s\n", c, t[c]/n[c], tc[c]/n[c], td[c]/n[c], m[c]/n[c], (c in bad ? "NAO" : "sim");
}' "$OUT" | head -1; awk -F, 'NR>1 {n[$2]++; t[$2]+=$5; tc[$2]+=$6; td[$2]+=$7; m[$2]+=$8; if($9!="sim") bad[$2]++}
END { for (c in n) printf "%-8s %8.3f %10.2f %10.2f %9.1f %8s\n", c, t[c]/n[c], tc[c]/n[c], td[c]/n[c], m[c]/n[c], (c in bad ? "NAO" : "sim"); }' "$OUT" | sort -k2 -nr; }