#!/usr/bin/env bash
# medir_parte3.sh — todas as medições da Parte III que dependem da máquina, nas
# condições da Secção 2.3 do relatório (Release, melhor de três execuções).
#
#   1. benchmark contra MP3 (LAME) e AAC (ffmpeg): débito real, taxa, SNR, tempos e
#      memória, a 32, 64, 96, 128, 192 e 256 kbit/s   (Tabelas 7 e 8, Figura 4)
#   2. versão anterior do codec (antes do PR #11) contra a atual   (Tabela 8)
#   3. alinhamento do MP3/AAC descodificado e efeito de 1 amostra de atraso (5.5)
#
# Os SNR e os tamanhos do nosso codec são determinísticos (iguais em qualquer
# máquina); o que muda de máquina para máquina são os tempos, a memória e, no MP3 e
# no AAC, a versão do ffmpeg.
#
# Uso:  scripts/medir_parte3.sh [pasta com os .wav]
#       (omissão: data/Audio_Datasets, ou data/train; procura os .wav em subpastas)
#       REPS=3 RATES="32 64 96 128 192 256" FFMPEG=ffmpeg scripts/medir_parte3.sh
# Saída: results/parte3/  (CSV + resumo.txt com as tabelas prontas a copiar)
set -u
export LC_ALL=C

REPS=${REPS:-3}
RATES=${RATES:-"32 64 96 128 192 256"}
FFMPEG=${FFMPEG:-ffmpeg}
OLD_COMMIT=2c4bbe1                       # main antes do PR #11 (versão em memória)
OUT=results/parte3
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"; git worktree remove --force "$TMP/old" >/dev/null 2>&1' EXIT
mkdir -p "$OUT"

# ---------------------------------------------------------------- ficheiros
DIR=${1:-}
if [ -z "$DIR" ]; then
  for d in data/Audio_Datasets data/train; do [ -d "$d" ] && { DIR=$d; break; }; done
fi
mapfile -t FILES < <(find "${DIR:-data}" -name '*.wav' | sort)
[ ${#FILES[@]} -gt 0 ] || { echo "Não encontrei ficheiros .wav (indica a pasta: $0 pasta)"; exit 1; }
command -v "$FFMPEG" >/dev/null || { echo "ffmpeg não encontrado: sudo apt install ffmpeg"; exit 1; }
python3 -c 'import numpy' 2>/dev/null || { echo "falta o numpy: pip install numpy"; exit 1; }

# ---------------------------------------------------------------- máquina
{
  echo "== máquina"
  lscpu | grep -E 'Model name|^CPU\(s\)'
  echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo '?')"
  echo "g++: $(g++ --version | head -1)"
  echo "ffmpeg: $("$FFMPEG" -version | head -1)"
  echo "commit: $(git rev-parse --short HEAD)   repetições: $REPS   ficheiros: ${#FILES[@]}"
} | tee "$OUT/maquina.txt"
gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo '?')
if [ "$gov" != "performance" ]; then
  echo
  echo "AVISO: o governor é '$gov' e não 'performance' (ver Secção 2.3). Para fixar:"
  echo "  echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"
  read -r -p "Continuar mesmo assim? [s/N] " r; [ "$r" = s ] || exit 1
fi

# ---------------------------------------------------------------- compilar
echo; echo "== a compilar a versão atual e a anterior ($OLD_COMMIT)"
cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j >/dev/null || exit 1
git worktree add -q --detach "$TMP/old" "$OLD_COMMIT" || exit 1
cmake -S "$TMP/old" -B "$TMP/old/b" -DCMAKE_BUILD_TYPE=Release >/dev/null &&
  cmake --build "$TMP/old/b" -j --target wav_lossy_enc wav_lossy_dec >/dev/null || exit 1
NEW=build; OLD=$TMP/old/b; CMP=build/wav_cmp

# melhor de REPS execuções: imprime "tempo_s memoria_KiB" (mínimo de cada)
best() {
  local t m bt=999999 bm=999999999
  for _ in $(seq "$REPS"); do
    /usr/bin/time -o "$TMP/tm" -f "%e %M" "$@" >/dev/null 2>&1 || return 1
    read -r t m < "$TMP/tm"
    bt=$(awk -v a="$t" -v b="$bt" 'BEGIN{print (a<b)?a:b}'); [ "$m" -lt "$bm" ] && bm=$m
  done
  echo "$bt $bm"
}
# SNR médio de L e R (dB), medido pelo wav_cmp
snr() { "$CMP" "$1" "$2" | awk '/SNR/ && $1!="MID"{s+=$(NF-1);k++} END{printf "%.4f",s/k}'; }

# ---------------------------------------------------------------- 1 e 2
B="$OUT/benchmark_lossy.csv"
echo "ficheiro,codec,kbps_pedido,bytes_orig,bytes_comp,kbps_real,taxa,snr_dB,t_comp_s,t_desc_s,mem_comp_KiB,mem_desc_KiB" > "$B"
echo; echo "== benchmark (${#FILES[@]} ficheiros x ${RATES// /,} kbit/s x 4 codecs x $REPS repetições)"
for f in "${FILES[@]}"; do
  n=$(basename "$f" .wav); orig=$(stat -c%s "$f")
  read -r frames rate < <("$CMP" "$f" "$f" | awk 'NR==1{print $2, $6}')
  fit="apad=whole_len=$frames,atrim=end_sample=$frames"
  for r in $RATES; do
    for codec in nosso anterior mp3 aac; do
      d="$TMP/d.wav"
      case $codec in
        nosso)    c="$TMP/c.icy"; e=$(best $NEW/wav_lossy_enc -r "$r" "$f" "$c"); dd=$(best $NEW/wav_lossy_dec "$c" "$d") ;;
        anterior) c="$TMP/c.icy"; e=$(best $OLD/wav_lossy_enc -r "$r" "$f" "$c"); dd=$(best $OLD/wav_lossy_dec "$c" "$d") ;;
        mp3)      c="$TMP/c.mp3"; e=$(best "$FFMPEG" -v error -y -i "$f" -c:a libmp3lame -b:a "${r}k" "$c")
                  dd=$(best "$FFMPEG" -v error -y -i "$c" -af "$fit" -c:a pcm_s16le "$d") ;;
        aac)      c="$TMP/c.m4a"; e=$(best "$FFMPEG" -v error -y -i "$f" -c:a aac -b:a "${r}k" "$c")
                  dd=$(best "$FFMPEG" -v error -y -i "$c" -af "$fit" -c:a pcm_s16le "$d") ;;
      esac
      comp=$(stat -c%s "$c"); s=$(snr "$f" "$d")
      awk -v n="$n" -v co="$codec" -v r="$r" -v o="$orig" -v c="$comp" -v fr="$frames" -v sr="$rate" \
          -v s="$s" -v e="$e" -v d="$dd" 'BEGIN{split(e,a," "); split(d,b," ");
          printf "%s,%s,%s,%d,%d,%.3f,%.3f,%s,%s,%s,%s,%s\n", n,co,r,o,c,c*8/(fr/sr)/1000,o/c,s,a[1],b[1],a[2],b[2]}' >> "$B"
    done
  done
  echo "  $n"
done

# ---------------------------------------------------------------- 3. alinhamento
echo; echo "== alinhamento do MP3/AAC (128 kbit/s)"
python3 - "$TMP" "$FFMPEG" "${FILES[@]}" > "$OUT/alinhamento.txt" <<'EOF'
import sys, wave, subprocess, numpy as np, os
tmp, ff, files = sys.argv[1], sys.argv[2], sys.argv[3:]
mix = [f for f in files if '_mix' in f][:2]; sq = [f for f in files if 'sqam' in f][:2]
def rd(p):
    w = wave.open(p); return np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, 2)[:, 0].astype(float)
print("ficheiro codec melhor_atraso snr_atraso0 snr_atraso1 snr_atraso2")
for f in mix + sq:
    x = rd(f); n = len(x)
    for codec, ext in (('mp3', 'mp3'), ('aac', 'm4a')):
        c, d = f'{tmp}/al.{ext}', f'{tmp}/al.wav'
        enc = ['-c:a', 'libmp3lame'] if codec == 'mp3' else ['-c:a', 'aac']
        subprocess.run([ff, '-v', 'error', '-y', '-i', f, *enc, '-b:a', '128k', c], check=True)
        subprocess.run([ff, '-v', 'error', '-y', '-i', c, '-af', f'apad=whole_len={n},atrim=end_sample={n}',
                        '-c:a', 'pcm_s16le', d], check=True)
        y = rd(d); a, b = 200000, min(1000000, n - 4000)
        best = max(range(-3000, 3001), key=lambda k: float(np.dot(x[a:b], y[a + k:b + k])))
        snr = lambda k: 10 * np.log10((x[a:b] ** 2).sum() / ((x[a:b] - y[a + k:b + k]) ** 2).sum())
        print(f"{os.path.basename(f)[:-4]} {codec} {best:+d} {snr(0):.2f} {snr(1):.2f} {snr(2):.2f}")
EOF
cat "$OUT/alinhamento.txt"

# ---------------------------------------------------------------- resumo
python3 scripts/plot_rd.py -i "$B" -o "$OUT/rd_lossy.png" >/dev/null 2>&1 || echo "(aviso: não foi possível desenhar a figura)"
{
  cat "$OUT/maquina.txt"; echo
  echo "== Tabela 7: SNR médio (dB) por débito pedido"
  awk -F, 'NR>1 && $2!="anterior"{c=($1~/^sqam/)?"SQAM":"DSD"; s[$3" "$2" "c]+=$8; n[$3" "$2" "c]++; s[$3" "$2]+=$8; n[$3" "$2]++; kb[$3" "$2]+=$6}
    END{printf "%6s %8s %8s %8s %8s %8s   (débito real AAC)\n","kbit/s","DSD100","SQAM","média","MP3","AAC";
    split("32 64 96 128 192 256",R," "); for(i=1;i<=6;i++){r=R[i]; if(!((r" nosso") in n)) continue;
    printf "%6s %8.2f %8.2f %8.2f %8.2f %8.2f   (%.1f)\n", r, s[r" nosso DSD"]/n[r" nosso DSD"], s[r" nosso SQAM"]/n[r" nosso SQAM"],
    s[r" nosso"]/n[r" nosso"], s[r" mp3"]/n[r" mp3"], s[r" aac"]/n[r" aac"], kb[r" aac"]/n[r" aac"]}}' "$B"
  echo
  echo "== Tabela 8: desempenho a 128 kbit/s (médias; memória em MiB, codificação / descodificação)"
  awk -F, 'NR>1 && $3==128{k=$2; n[k]++; kb[k]+=$6; s[k]+=$8; tc[k]+=$9; td[k]+=$10; mc[k]+=$11; md[k]+=$12; if($11>mx[k])mx[k]=$11}
    END{printf "%-10s %8s %8s %8s %8s %16s %10s\n","codec","kbit/s","SNR","t_cod","t_desc","memória","mem.máx";
    split("nosso anterior mp3 aac",C," "); for(i=1;i<=4;i++){k=C[i]; if(!(k in n)) continue;
    printf "%-10s %8.1f %8.2f %8.2f %8.3f %7.1f / %-6.1f %10.1f\n", k, kb[k]/n[k], s[k]/n[k], tc[k]/n[k], td[k]/n[k], mc[k]/n[k]/1024, md[k]/n[k]/1024, mx[k]/1024}}' "$B"
  echo
  echo "== Alinhamento (melhor atraso tem de ser 0; atraso1/atraso2 = SNR com 1 e 2 amostras de desfasamento)"
  cat "$OUT/alinhamento.txt"
} | tee "$OUT/resumo.txt"
echo; echo "Resultados em $OUT/ (benchmark_lossy.csv, resumo.txt, alinhamento.txt, rd_lossy.png)"
