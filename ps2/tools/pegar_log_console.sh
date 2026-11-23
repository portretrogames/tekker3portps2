#!/usr/bin/env bash
# Traz o log do console (tekken3_log.txt) para "ELF OU ISO TESTE/resultados_logs"
# com o nome da versão e compara com o log do console da versão anterior.
#
# Uso (de dentro de Codigo/ps2/):
#   bash tools/pegar_log_console.sh            procura no pendrive (/media/$USER/*/APPS/TEKKEN3)
#   bash tools/pegar_log_console.sh ARQUIVO    usa este arquivo (ex.: copiado à mão)
# Com BASE=arquivo compara com outro log em vez do da versão anterior.
# Nunca apaga nem muda o log do pendrive. Se já houver um log diferente da mesma
# versão, guarda como _2, _3...; se for o mesmo arquivo, só mostra a comparação.
set -euo pipefail
# Caminhos dados pelo usuário valem a partir da pasta onde ele está.
ORIG="${1:+$(realpath -m -- "$1")}"
if [ -n "${BASE:-}" ]; then
  BASE=$(realpath -m -- "$BASE")
  [ -f "$BASE" ] || { echo "BASE não existe: $BASE" >&2; exit 1; }
fi
cd "$(dirname "$0")/.."
DEST="../../ELF OU ISO TESTE/resultados_logs"
mkdir -p "$DEST"

if [ -z "$ORIG" ]; then
  # V53: o log leva a versão no nome (tekken3_log_V53.txt) e pode estar numa
  # pasta de teste do OPL (APPS/T3DIAG...): pega o mais novo
  ORIG=$(ls -t /media/"$USER"/*/APPS/*/tekken3_log*.txt \
               /run/media/"$USER"/*/APPS/*/tekken3_log*.txt 2>/dev/null | head -n1 || true)
  if [ -z "$ORIG" ]; then
    echo "Não achei o pendrive com APPS/TEKKEN3/tekken3_log.txt." >&2
    echo "Conecte o pendrive ou passe o arquivo: bash tools/pegar_log_console.sh caminho/tekken3_log.txt" >&2
    exit 1
  fi
fi
[ -f "$ORIG" ] || { echo "não achei o arquivo: $ORIG" >&2; exit 1; }
V=$(sed -n 's/^log: versao T3 PS2 \(V[0-9A-Za-z]*\).*/\1/;T;p;q' "$ORIG")
[ -n "$V" ] || { echo "o arquivo não tem a linha \"log: versao T3 PS2 Vxx\" (é o log do port?)" >&2; exit 1; }
echo "==> Log do console da $V: $ORIG"

NOVO=""
for f in "$DEST"/tekken3_log_*_console*.txt; do
  if [ -f "$f" ] && cmp -s "$f" "$ORIG"; then
    NOVO="$f"; echo "==> Já estava guardado: $(basename "$f")"; break
  fi
done
if [ -z "$NOVO" ]; then
  NOVO="$DEST/tekken3_log_${V}_console.txt"; n=2
  while [ -e "$NOVO" ]; do NOVO="$DEST/tekken3_log_${V}_console_$n.txt"; n=$((n + 1)); done
  cp -p "$ORIG" "$NOVO.novo" && mv "$NOVO.novo" "$NOVO"
  echo "==> Guardado: $(realpath "$NOVO")"
fi

# Anterior: o log do console da maior versão abaixo desta. Ordena pela versão
# (V9 < V10 < V41 < V41d < V42), não pelo nome do arquivo: no nome, "V41d_" vem
# antes de "V41_". Entre logs da mesma versão fica o último (_2, _3...).
ANT="${BASE:-}"
if [ -z "$ANT" ]; then
  TAB=$(printf '\t')
  while IFS="$TAB" read -r vf f; do
    if [ "$vf" != "$V" ] && [ "$(printf '%s\n%s\n' "$vf" "$V" | sort -V | head -n1)" = "$vf" ]; then
      ANT="$f"
    fi
  done < <(for f in "$DEST"/tekken3_log_V*_console*.txt; do
             [ -f "$f" ] || continue
             vf=$(basename "$f" | sed -n 's/^tekken3_log_\(V[0-9A-Za-z]*\)_console.*/\1/p')
             [ -n "$vf" ] && printf '%s\t%s\n' "$vf" "$f"
           done | sort -t"$TAB" -k1,1V -k2,2V)
fi
echo
if [ -n "$ANT" ]; then
  python3 tools/resumo_log.py "$ANT" "$NOVO"
else
  echo "(nenhum log de console de versão anterior em resultados_logs: sem comparação)"
  python3 tools/resumo_log.py "$NOVO"
fi
