#!/usr/bin/env bash
# Roda o tekken3.elf no PCSX2 Linux sem tela (Xvfb) e grava o log.
# Uso: bash ps2/tools/rodar_pcsx2_linux.sh [segundos=40] [outro.elf]
# Requer: PCSX2 AppImage extraído em $PCSX2_DIR (squashfs-root) e as libs
# OpenGL/EGL/xcb em $LIBS_DIR (extraídas de .deb com apt-get download).
# Config em ~/.config/PCSX2 (HostFs ligado, BIOS real, renderer Software).
set -u
SEG="${1:-40}"
ELF_ARG="${2:-}"
AQUI="$(cd "$(dirname "$0")/.." && pwd)"
PCSX2_DIR="${PCSX2_DIR:-$HOME/work/squashfs-root}"
LIBS_DIR="${LIBS_DIR:-$HOME/work/debs/root/usr/lib/x86_64-linux-gnu}"
LOG="$AQUI/docs/ultimo_log_pcsx2.txt"
TELA="$AQUI/build/tela_pcsx2.png"

# Libs extraídas à parte (máquinas sem OpenGL instalado). No Ubuntu com as libs
# do sistema, NÃO usar LD_LIBRARY_PATH: troca o Qt do AppImage pelo do sistema.
if [ -d "$LIBS_DIR/dri" ] && [ "$LIBS_DIR" != /usr/lib/x86_64-linux-gnu ]; then
  export LD_LIBRARY_PATH="$LIBS_DIR"
  export LIBGL_DRIVERS_PATH="$LIBS_DIR/dri"
  export __EGL_VENDOR_LIBRARY_DIRS="$LIBS_DIR/../../share/glvnd/egl_vendor.d"
fi
export QT_QPA_PLATFORM=xcb
# Esconde o Vulkan: na tela virtual (Xvfb) ele falha ("no present queue").
# Sem ele o PCSX2 apresenta a imagem do renderizador por software via OpenGL.
export VK_DRIVER_FILES=/nonexistent VK_ICD_FILENAMES=/nonexistent
DISP=":$((90 + RANDOM % 100))"
Xvfb "$DISP" -screen 0 1280x720x24 >/dev/null 2>&1 & XP=$!
sleep 2
export DISPLAY="$DISP"
rm -f "$LOG"
"$PCSX2_DIR/usr/bin/pcsx2-qt" -earlyconsolelog -logfile "$LOG" -batch \
    -fastboot -- "${ELF_ARG:-$AQUI/tekken3.elf}" >/dev/null 2>&1 & P=$!
sleep "$SEG"
mkdir -p "$AQUI/build"
LD_LIBRARY_PATH= import -window root "$TELA" 2>/dev/null
kill -9 "$P" 2>/dev/null; kill "$XP" 2>/dev/null
echo "=== Saida do port (log completo: docs/ultimo_log_pcsx2.txt) ==="
sed -n '/Tekken 3 PS2/,/Restart\./p' "$LOG" | grep -v "Restart\." | head -80
