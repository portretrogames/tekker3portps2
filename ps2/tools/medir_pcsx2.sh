#!/usr/bin/env bash
# Mede a versão atual no PCSX2 (luta automática, sem tela) e compara com a
# medida anterior.
#
# Uso (de dentro de Codigo/ps2/):  bash tools/medir_pcsx2.sh [segundos=230] [rótulo]
#   1. compila com AUTO_START=1 LOG_BLOCOS=15 (muda só o ps2/tekken3.elf de teste;
#      os ELFs de "ELF OU ISO TESTE" e do pendrive não são tocados);
#   2. roda o PCSX2 sem tela (tools/rodar_pcsx2_linux.sh);
#   3. guarda o log em "ELF OU ISO TESTE/resultados_logs/pcsx2/VXX_AAAA-MM-DD_HHMM[_rótulo].txt";
#   4. compara com a medida anterior dessa pasta (ou com BASE=arquivo) pelo
#      tools/resumo_log.py.
# O PCSX2 não tem o tempo do console (memória e GS custam diferente): serve para
# comparar uma versão com a outra; o número que vale é o do console.
# Com a luta automática o PCSX2 é determinístico: o mesmo ELF medido duas vezes
# deu os mesmos números (19/11/2026), então qualquer diferença vem da mudança.
set -euo pipefail
# BASE vale a partir da pasta onde o usuário está.
if [ -n "${BASE:-}" ]; then
  BASE=$(realpath -m -- "$BASE")
  [ -f "$BASE" ] || { echo "BASE não existe: $BASE" >&2; exit 1; }
fi
cd "$(dirname "$0")/.."
SEG="${1:-230}"
ROT="${2:-}"
case "$SEG" in ''|*[!0-9]*) echo "uso: bash tools/medir_pcsx2.sh [segundos] [rótulo]" >&2; exit 1 ;; esac
case "$ROT" in *[!A-Za-z0-9_-]*) echo "rótulo: só letras, números, - e _" >&2; exit 1 ;; esac
: "${PS2DEV:=$HOME/ps2dev/ps2dev}"
: "${PS2SDK:=$PS2DEV/ps2sdk}"
export PS2DEV PS2SDK PATH="$PATH:$PS2DEV/bin:$PS2DEV/ee/bin"

VERSAO=$(sed -n 's/^#define *JOGO_VERSAO_NUM *"\(V[0-9A-Za-z]*\)".*/\1/;T;p;q' runtime/jogo.h)
[ -n "$VERSAO" ] || { echo "erro: não achei JOGO_VERSAO em runtime/jogo.h" >&2; exit 1; }
PASTA="../../ELF OU ISO TESTE/resultados_logs/pcsx2"
mkdir -p "$PASTA" build
# Medida anterior: a mais recente da pasta (antes de gravar a nova).
ANT="${BASE:-$(ls -t "$PASTA"/*.txt 2>/dev/null | head -n1 || true)}"

echo "==> Compilando $VERSAO para o PCSX2 (AUTO_START=1 LOG_BLOCOS=15)"
if ! make -j"$(nproc)" AUTO_START=1 LOG_BLOCOS=15 > build/medir_pcsx2_make.txt 2>&1; then
  tail -30 build/medir_pcsx2_make.txt; echo "!! compilação falhou (build/medir_pcsx2_make.txt)" >&2; exit 1
fi
echo "==> Rodando no PCSX2 por $SEG s (luta automática)"
LOG=docs/ultimo_log_pcsx2.txt
if [ -n "$ANT" ] && [ "$(realpath -m -- "$ANT")" = "$(realpath -m -- "$LOG")" ]; then
  cp "$LOG" build/medir_pcsx2_base.txt; ANT=build/medir_pcsx2_base.txt   # BASE = o log que vai ser regravado
fi
rm -f "$LOG"   # um log velho não pode passar por medida nova
bash tools/rodar_pcsx2_linux.sh "$SEG" > /dev/null
if ! grep -Eq "Tekken 3 PS2 - T3 PS2 ${VERSAO}([[:space:]]|\$)" "$LOG" 2>/dev/null; then
  echo "!! o PCSX2 não rodou o port (sem \"T3 PS2 $VERSAO\" em $LOG)" >&2; exit 1
fi
# Sem janela de luta o log não serve de medida (nem de base para a próxima).
if ! grep -Eq "logica: atualizacoes da luta \(8002ae58\) [1-9]" "$LOG"; then
  echo "!! o log não chegou à luta (rode mais tempo; 230 s chegam). Não foi guardado:" >&2
  echo "   python3 tools/resumo_log.py $LOG" >&2
  exit 1
fi
NOVO="$PASTA/${VERSAO}_$(date +%Y-%m-%d_%H%M)${ROT:+_$ROT}.txt"
n=2; BASE_NOVO="${NOVO%.txt}"
while [ -e "$NOVO" ]; do NOVO="${BASE_NOVO}_$n.txt"; n=$((n + 1)); done
cp "$LOG" "$NOVO"
echo "==> Log guardado: $(realpath "$NOVO")"
echo
if [ -n "$ANT" ] && [ -f "$ANT" ]; then
  python3 tools/resumo_log.py "$ANT" "$NOVO"
else
  echo "(primeira medida em resultados_logs/pcsx2: sem comparação)"
  python3 tools/resumo_log.py "$NOVO"
fi
