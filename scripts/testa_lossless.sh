#!/usr/bin/env bash
# Bateria de casos-limite para qualquer variante.
#   ./testa.sh "<comando do codificador>" "<comando do descodificador>"
cd "$(dirname "$0")/.."
mkdir -p out
ENC="$1"; DEC="$2"
ok=0; mau=0
# um ficheiro real qualquer, para os testes de tamanho de bloco e particoes
REAL=$(find data -name '*.wav' -size +1M 2>/dev/null | head -1)
[ -z "$REAL" ] && REAL=$(find data -name '*.wav' 2>/dev/null | head -1)
if [ -z "$REAL" ]; then echo "nao encontrei nenhum .wav em data/"; exit 1; fi
echo "ficheiro de referencia: $REAL"

verifica() {  # verifica <ficheiro> <descricao> [flags extra]
  local W="$1"; local desc="$2"; shift 2
  local msg
  if ! msg=$($ENC "$@" "$W" out/tt.bin 2>&1 >/dev/null); then
    printf '  %-32s ENC RECUSOU: %s\n' "$desc" "$(echo "$msg" | head -1)"; mau=$((mau+1)); return
  fi
  if ! msg=$($DEC out/tt.bin out/tt.wav 2>&1 >/dev/null); then
    printf '  %-32s DEC FALHOU: %s\n' "$desc" "$(echo "$msg" | head -1)"; mau=$((mau+1)); return
  fi
  if cmp -s "$W" out/tt.wav; then
    printf '  %-32s OK   %8d -> %8d\n' "$desc" "$(stat -c%s "$W")" "$(stat -c%s out/tt.bin)"
    ok=$((ok+1))
  else
    printf '  %-32s DIFERE do original\n' "$desc"; mau=$((mau+1))
  fi
}

echo "--- formatos"
verifica testes/mono16.wav   "mono, 16 bits"
verifica testes/oito.wav     "estereo, 8 bits"
verifica testes/vf24.wav     "estereo, 24 bits"
verifica testes/tv32.wav     "estereo, 32 bits"
echo "--- tamanhos extremos"
verifica testes/curto1.wav   "1 frame"
verifica testes/curto2.wav   "2 frames (< historico)"
verifica testes/curto5.wav   "5 frames"
verifica testes/impar.wav    "data com bytes impares"
echo "--- contentor"
verifica testes/meta.wav     "com bloco LIST de metadados"
verifica testes/truncado.wav "data declarado maior que o real"
echo "--- tamanhos de bloco"
for b in 16 17 1024 65535; do
  verifica "$REAL" "bloco $b" -b "$b"
done
echo "--- casos do tipo SQAM"
verifica testes/silencio.wav  "silencio total"
verifica testes/dualmono.wav  "canais identicos (dual mono)"
verifica testes/umcanal.wav   "um canal em silencio"
verifica testes/quadrada.wav  "onda quadrada saturada"
verifica testes/tom.wav       "tom puro"
echo "--- determinismo das threads"
W=data/sample03.wav
$ENC --threads 1 "$W" out/d1.bin >/dev/null 2>&1
for n in 2 4 8; do
  $ENC --threads $n "$W" out/d$n.bin >/dev/null 2>&1
  if cmp -s out/d1.bin out/d$n.bin; then
    printf '  %-32s identico a 1 thread\n' "$n threads"; ok=$((ok+1))
  else printf '  %-32s BITSTREAM DIFERENTE\n' "$n threads"; mau=$((mau+1)); fi
done
echo "--- robustez"
head -c 2000 /dev/urandom > out/lixo.bin
$DEC out/lixo.bin out/l.wav >/dev/null 2>&1 \
  && { printf '  %-32s ACEITOU lixo\n' "ficheiro aleatorio"; mau=$((mau+1)); } \
  || { printf '  %-32s recusou\n' "ficheiro aleatorio"; ok=$((ok+1)); }
head -c 1000 out/d1.bin > out/meio.bin
$DEC out/meio.bin out/m.wav >/dev/null 2>&1 \
  && printf '  %-32s nao deu erro\n' "comprimido truncado" \
  || { printf '  %-32s recusou\n' "comprimido truncado"; ok=$((ok+1)); }
$ENC testes/nao_existe.wav out/x.bin >/dev/null 2>&1 \
  && { printf '  %-32s ACEITOU inexistente\n' "input inexistente"; mau=$((mau+1)); } \
  || { printf '  %-32s recusou\n' "input inexistente"; ok=$((ok+1)); }

echo
echo "passou: $ok   falhou: $mau"