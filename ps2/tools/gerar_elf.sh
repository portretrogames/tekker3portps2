#!/usr/bin/env bash
# Compila o port e entrega o ELF pronto em "ELF OU ISO TESTE/APPS/TEKKEN3"
# (a mesma pasta do pendrive) e copia para o pendrive, se estiver montado em
# /media/$USER/*/APPS/TEKKEN3.
#
# Uso (de dentro de Codigo/ps2/):  bash tools/gerar_elf.sh [jogo|log|medida]
#   jogo   (padrão) sem log e sem medição (LIMPO=1, sem painel): para jogar -> tekken3.elf
#   log    painel + log no pendrive a cada ~30 s (trava ~0,5 s ao gravar) -> tekken3_log.elf
#   medida log + perfil das chamadas diretas (mais lento)            -> tekken3_medida.elf
# A versão anterior de cada arquivo vai para "ELF OU ISO TESTE/antigos"
# (tekken3_VXX_modo.elf). VERSAO.txt diz a versão de cada arquivo.
set -euo pipefail
cd "$(dirname "$0")/.."
MODO="${1:-jogo}"
case "$MODO" in
  jogo)   ARGS=("LOG_BLOCOS=0" "LIMPO=1");  NOME=tekken3.elf ;;
  log)    ARGS=("LOG_BLOCOS=15" "LIMPO=0"); NOME=tekken3_log.elf ;;
  medida) ARGS=("LOG_BLOCOS=15" "LIMPO=0" "GEN_OPT=-O2 -DPS2_PROF_DIRETO=1"); NOME=tekken3_medida.elf ;;
  *) echo "uso: bash tools/gerar_elf.sh [jogo|log|medida]"; exit 1 ;;
esac
VERSAO=$(sed -n 's/.*JOGO_VERSAO_NUM *"\(V[0-9A-Za-z]*\)".*/\1/p' runtime/jogo.h | head -n1)
[ -n "$VERSAO" ] || { echo "erro: não achei JOGO_VERSAO_NUM (\"Vxx\") em runtime/jogo.h" >&2; exit 1; }
echo "==> Compilando $VERSAO ($MODO): make ${ARGS[*]}"
echo "    (se mudou o código gerado ou a ferramenta, leva ~3-5 min)"
make -j"$(nproc)" "${ARGS[@]}"
make pacote "${ARGS[@]}"

PACOTE=build/pacote/APPS/TEKKEN3
ELF="$PACOTE/tekken3.elf"
TESTE="../../ELF OU ISO TESTE"
DEST="$TESTE/APPS/TEKKEN3"
VERSOES="$DEST/VERSAO.txt"
mkdir -p "$DEST" "$TESTE/antigos"
touch "$VERSOES"
# Copia primeiro para um temporário: se falhar, o ELF que já estava fica no lugar.
cp "$ELF" "$DEST/$NOME.novo"
V_ANT=$(sed -n "s/^$MODO=//p" "$VERSOES" | head -n1)
if [ -f "$DEST/$NOME" ] && [ -n "$V_ANT" ] && [ "$V_ANT" != "$VERSAO" ]; then
  cp -p "$DEST/$NOME" "$TESTE/antigos/tekken3_${V_ANT}_${MODO}.elf"
  echo "==> $V_ANT ($MODO) guardada em ELF OU ISO TESTE/antigos"
fi
mv -f "$DEST/$NOME.novo" "$DEST/$NOME"
cp "$PACOTE/title.cfg" "$PACOTE/LEIA-ME.md" "$DEST/"
{ grep -v "^$MODO=" "$VERSOES" || true; echo "$MODO=$VERSAO"; } | sort > "$VERSOES.tmp"
mv "$VERSOES.tmp" "$VERSOES"
echo "==> Pronto: $(realpath "$DEST/$NOME")"

# Pendrive: só o ELF deste modo (o title.cfg do OPL abre o tekken3.elf; o
# tekken3_log.elf abre pelo wLaunchELF). Copia para temporário e troca no fim.
ACHOU=0; FALHOU=0
for d in /media/"$USER"/*/APPS/TEKKEN3 /run/media/"$USER"/*/APPS/TEKKEN3; do
  [ -d "$d" ] || continue
  ACHOU=1
  if cp "$ELF" "$d/$NOME.novo" && mv -f "$d/$NOME.novo" "$d/$NOME" && sync; then
    echo "==> Copiado para o pendrive: $d/$NOME"
  else
    rm -f "$d/$NOME.novo"; FALHOU=1
    echo "!! Falhou a cópia para o pendrive ($d). O $NOME de lá não foi trocado." >&2
  fi
done
[ $ACHOU = 1 ] || echo "    (pendrive não encontrado em /media/$USER/*/APPS/TEKKEN3: copie o arquivo acima à mão)"
echo "    No console o painel deve mostrar \"T3 PS2 $VERSAO\"."
[ $FALHOU = 0 ] || exit 1
