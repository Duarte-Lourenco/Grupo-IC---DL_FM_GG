#!/usr/bin/env bash
# benchmark_lossy.sh — compara o nosso codec com perdas com MP3 e AAC.
#
# Para cada ficheiro, cada débito e cada codec mede: tamanho comprimido, taxa
# de compressão, SNR (média de L e R, com o wav_cmp), tempo de compressão e de
# descompressão (s) e memória máxima (MiB).
#
# MP3 = libmp3lame (CBR), AAC = codificador nativo do ffmpeg (em .m4a). Os
# descodificadores do ffmpeg já retiram o atraso do codificador; o apad/atrim
# só acerta o n.º de amostras para o wav_cmp poder comparar.
#
# Uso:  scripts/benchmark_lossy.sh [ficheiros.wav...]      (omisso: data/*.wav)
#       FFMPEG=/caminho/ffmpeg RATES="64 128" scripts/benchmark_lossy.sh
# Saída: results/benchmark_lossy.csv  +  tabela resumo no ecrã
set -u
export LC_ALL=C

ENC=build/wav_lossy_enc
DEC=build/wav_lossy_dec
CMP=build/wav_cmp
FFMPEG=${FFMPEG:-ffmpeg}
RATES=${RATES:-"32 64 96 128 192 256"}
OUT=results/benchmark_lossy.csv
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

[ -x "$ENC" ] && [ -x "$DEC" ] && [ -x "$CMP" ] || { echo "Compila primeiro: cmake --build build"; exit 1; }
command -v "$FFMPEG" >/dev/null || { echo "ffmpeg não encontrado (sudo apt install ffmpeg, ou FFMPEG=...)"; exit 1; }
mkdir -p results

FILES=("$@")
[ ${#FILES[@]} -eq 0 ] && FILES=(data/*.wav)

# corre um comando; devolve "tempo_s memoria_MiB"
run() {
  local t
  t=$(/usr/bin/time -f "%e %M" "$@" 2>&1 >/dev/null | tail -1)
  echo "$t" | awk '{printf "%.2f %.1f", $1, $2/1024}'
}

# SNR médio dos canais (sem a linha do MID)
snr() {
  "$CMP" "$1" "$2" | awk '/SNR/ && $1 != "MID" {s += $NF == "dB" ? $(NF-1) : $NF; n++} END {printf "%.2f", s/n}'
}

echo "ficheiro,codec,kbps_pedido,bytes_orig,bytes_comp,kbps_real,taxa,snr_dB,t_comp_s,t_desc_s,mem_comp_MiB,mem_desc_MiB" > "$OUT"

for f in "${FILES[@]}"; do
  [ -f "$f" ] || continue
  name=$(basename "$f" .wav)
  orig=$(stat -c%s "$f")
  # n.º de frames e duração, a partir do cabeçalho que o wav_cmp mostra
  frames=$("$CMP" "$f" "$f" | awk 'NR==1 {print $2}')
  rate=$("$CMP" "$f" "$f" | awk 'NR==1 {print $6}')
  fit="apad=whole_len=$frames,atrim=end_sample=$frames"

  for r in $RATES; do
    for codec in nosso mp3 aac; do
      d="$TMP/$name.$codec.wav"
      case $codec in
        nosso) c="$TMP/$name.icy"
               ce=$(run $ENC -r "$r" "$f" "$c"); cd=$(run $DEC "$c" "$d") ;;
        mp3)   c="$TMP/$name.mp3"
               ce=$(run "$FFMPEG" -v error -y -i "$f" -c:a libmp3lame -b:a "${r}k" "$c")
               cd=$(run "$FFMPEG" -v error -y -i "$c" -af "$fit" -c:a pcm_s16le "$d") ;;
        aac)   c="$TMP/$name.m4a"
               ce=$(run "$FFMPEG" -v error -y -i "$f" -c:a aac -b:a "${r}k" "$c")
               cd=$(run "$FFMPEG" -v error -y -i "$c" -af "$fit" -c:a pcm_s16le "$d") ;;
      esac
      comp=$(stat -c%s "$c")
      s=$(snr "$f" "$d")
      awk -v n="$name" -v co="$codec" -v r="$r" -v o="$orig" -v c="$comp" -v fr="$frames" -v sr="$rate" \
          -v s="$s" -v ce="$ce" -v cd="$cd" 'BEGIN {
        split(ce, a, " "); split(cd, b, " ");
        printf "%s,%s,%s,%d,%d,%.1f,%.2f,%s,%s,%s,%s,%s\n", n, co, r, o, c, c*8/(fr/sr)/1000, o/c, s, a[1], b[1], a[2], b[2]
      }' >> "$OUT"
      rm -f "$c" "$d"
    done
  done
done

echo
echo "Resultados em $OUT"
echo
awk -F, 'NR>1 {k=$3" "$2; n[k]++; kb[k]+=$6; s[k]+=$8; tc[k]+=$9; td[k]+=$10; m[k]+=$11}
END {
  printf "%6s %-6s %9s %9s %10s %10s %9s\n","kbps","codec","kbps_real","SNR(dB)","t_comp(s)","t_desc(s)","mem(MiB)";
  for (k in n) { split(k, p, " ");
    printf "%6s %-6s %9.1f %9.2f %10.2f %10.2f %9.1f\n", p[1], p[2], kb[k]/n[k], s[k]/n[k], tc[k]/n[k], td[k]/n[k], m[k]/n[k] }
}' "$OUT" | sort -k1,1n -k2,2 | (read -r h; echo "$h"; cat)
