#!/usr/bin/env bash
# Reproduz a Fase 1 em Linux/WSL:
#  1) compila o recompilador (psxrecomp-game)
#  2) gera o C do SLUS_004.02 a partir do seu dump
#  3) compila todo o C gerado para o EE do PS2 (teste de viabilidade)
#
# Uso: bash ps2/tools/gerar_e_compilar_ee.sh /caminho/ps2dev
# Requisitos: git, gcc/g++, python3, cmake>=3.20, ninja
#   (pip install --user cmake ninja), toolchain ps2dev
#   (https://github.com/ps2dev/ps2dev/releases -> ps2dev-ubuntu-latest.tar.gz)
set -euo pipefail
PS2DEV="${1:?informe o caminho do ps2dev (pasta que contém ee/bin)}"
RAIZ="$(cd "$(dirname "$0")/../.." && pwd)"
REF="$RAIZ/referencia/Tekken3Recompiled"
EE="$PS2DEV/ee/bin/mips64r5900el-ps2-elf"

cd "$REF"
# Os scripts vêm com fim de linha do Windows (CRLF)
tr -d '\r' < psxrecomp/tools/ci/build_emitters.sh > /tmp/build_emitters.sh
bash /tmp/build_emitters.sh --jobs "$(nproc)"

# Sementes extras: funções chamadas só por ponteiro (achadas pelo port)
if ! grep -q "scan do port PS2" seeds/ghidra_funcs.txt; then
  { echo "# Funcoes chamadas por ponteiro (scan do port PS2)";
    cat "$RAIZ/ps2/tools/sementes_ponteiros.txt"; } >> seeds/ghidra_funcs.txt
fi

mkdir -p disc
# O dump do jogo fica em "tekken 3 ps2/ROM" (fora do Codigo)
ROM="$RAIZ/../ROM"
[ -f "$ROM/Tekken 3 (USA).cue" ] || { echo "erro: não achei \"Tekken 3 (USA).cue\" em $ROM" >&2; exit 1; }
for f in "$ROM"/*.bin "$ROM"/*.cue; do ln -sf "$(realpath "$f")" "disc/$(basename "$f")"; done
python3 psxrecomp/psxrecomp_cli.py generate --config game.toml \
  --project-root . --disc "disc/Tekken 3 (USA).cue" --no-toolchain-download

cp generated/SLUS_004.02_* "$RAIZ/gerado/"
OUT="$RAIZ/build-ee"; mkdir -p "$OUT"
for f in generated/SLUS_004.02_full_*.c generated/SLUS_004.02_dispatch.c; do
  "$EE-gcc" -c -O2 -G0 -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 -w \
    -I generated -I psxrecomp/runtime/include -I psxrecomp/runtime/src -I . \
    "$f" -o "$OUT/$(basename "$f" .c).o"
done
"$EE-size" -t "$OUT"/*.o | tail -1
echo "Simbolos que o runtime do PS2 precisa fornecer:"
"$EE-nm" -u "$OUT"/*.o | awk 'NF==2{print $2}' | sort -u > /tmp/u.txt
"$EE-nm" --defined-only "$OUT"/*.o | awk 'NF==3{print $3}' | sort -u > /tmp/d.txt
comm -23 /tmp/u.txt /tmp/d.txt
