#!/usr/bin/env bash
# Compila o port e entrega o ELF pronto em "ELF OU ISO TESTE/APPS/TEKKEN3"
# (a mesma pasta do pendrive) e copia para o pendrive, se estiver montado em
# /media/$USER/*/APPS/TEKKEN3.
#
# Uso (de dentro de Codigo/ps2/):  bash tools/gerar_elf.sh
# O ELF sai sem as informações de depuração (make pacote). A versão anterior
# vai para "ELF OU ISO TESTE/antigos" (tekken3_VXX.elf).
set -euo pipefail
cd "$(dirname "$0")/.."
VERSAO=$(sed -n 's/.*JOGO_VERSAO_NUM *"\(V[0-9A-Za-z]*\)".*/\1/p' runtime/jogo.h | head -n1)
[ -n "$VERSAO" ] || { echo "erro: não achei JOGO_VERSAO_NUM (\"Vxx\") em runtime/jogo.h" >&2; exit 1; }
echo "==> Compilando $VERSAO"
echo "    (se mudou o código gerado ou a ferramenta, leva ~3-5 min)"
make -j"$(nproc)"
make pacote

PACOTE=build/pacote/APPS/TEKKEN3
ELF="$PACOTE/tekken3.elf"
TESTE="../../ELF OU ISO TESTE"
DEST="$TESTE/APPS/TEKKEN3"
VERSOES="$DEST/VERSAO.txt"
mkdir -p "$DEST" "$TESTE/antigos"
touch "$VERSOES"
# Copia primeiro para um temporário: se falhar, o ELF que já estava fica no lugar.
cp "$ELF" "$DEST/tekken3.elf.novo"
V_ANT=$(sed -n "s/^jogo=//p" "$VERSOES" | head -n1)
if [ -f "$DEST/tekken3.elf" ] && [ -n "$V_ANT" ] && [ "$V_ANT" != "$VERSAO" ]; then
  cp -p "$DEST/tekken3.elf" "$TESTE/antigos/tekken3_${V_ANT}.elf"
  echo "==> $V_ANT guardada em ELF OU ISO TESTE/antigos"
fi
mv -f "$DEST/tekken3.elf.novo" "$DEST/tekken3.elf"
cp "$PACOTE/title.cfg" "$PACOTE/LEIA-ME.txt" "$DEST/"
rm -f "$DEST/LEIA-ME.md"      # até a V60 o LEIA-ME era .md
cp build/pacote/Criar_Tekken3_PS2.html "$TESTE/"
echo "jogo=$VERSAO" > "$VERSOES"
echo "==> Pronto: $(realpath "$DEST/tekken3.elf")"

# Pendrive: copia para temporário e troca no fim.
ACHOU=0; FALHOU=0
for d in /media/"$USER"/*/APPS/TEKKEN3 /run/media/"$USER"/*/APPS/TEKKEN3; do
  [ -d "$d" ] || continue
  ACHOU=1
  if cp "$ELF" "$d/tekken3.elf.novo" && mv -f "$d/tekken3.elf.novo" "$d/tekken3.elf" && sync; then
    echo "==> Copiado para o pendrive: $d/tekken3.elf"
  else
    rm -f "$d/tekken3.elf.novo"; FALHOU=1
    echo "!! Falhou a cópia para o pendrive ($d). O tekken3.elf de lá não foi trocado." >&2
  fi
done
[ $ACHOU = 1 ] || echo "    (pendrive não encontrado em /media/$USER/*/APPS/TEKKEN3: copie o arquivo acima à mão)"
[ $FALHOU = 0 ] || exit 1
