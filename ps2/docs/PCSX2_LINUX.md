# Rodar o PCSX2 no Linux (ou WSL) sem tela — testes automáticos do port

Automatizado em `ps2/tools/rodar_pcsx2_linux.sh`. Este documento explica o
que o script faz e como montar o ambiente. Funciona num Ubuntu 22.04 (WSL ou
o ambiente Linux do app do Claude).

## 1. Baixar e extrair o PCSX2 (AppImage oficial)

```bash
mkdir -p ~/work && cd ~/work
curl -L -o pcsx2.AppImage \
  https://github.com/PCSX2/pcsx2/releases/download/v2.9.179/pcsx2-v2.9.179-linux-appimage-x64-Qt.AppImage
chmod +x pcsx2.AppImage
./pcsx2.AppImage --appimage-extract   # cria ~/work/squashfs-root
```

Extrair evita depender de FUSE. Executável: `squashfs-root/usr/bin/pcsx2-qt`.

## 2. Bibliotecas que faltam (OpenGL/EGL e Qt xcb)

Erros vistos sem elas: `libOpenGL.so.0`, `libEGL.so.1`, plugin Qt `xcb`
(pede `libxcb-cursor0` e `libxcb-util1`).

**Com sudo (WSL):**
```bash
sudo apt install libopengl0 libegl1 libgl1-mesa-dri libxcb-cursor0 libxcb-util1 \
  libxcb-xinput0 libxkbcommon-x11-0 libxcb-icccm4 libxcb-keysyms1 libxcb-image0 \
  libxcb-render-util0 xvfb imagemagick
```

**Sem sudo:** baixar os .deb e extrair numa pasta (sem instalar):
```bash
mkdir -p ~/work/debs && cd ~/work/debs
apt-get download libopengl0 libegl1 libglvnd0 libegl-mesa0 libgbm1 libgl1-mesa-dri \
  libglx0 libglx-mesa0 libgl1 libglapi-mesa libxcb-xinput0 libxkbcommon-x11-0 \
  libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-image0 libxcb-render-util0 \
  libxcb-util1 libxcb-shape0 libxcb-randr0 libxcb-xkb1 libxcb-render0 libxcb-shm0 \
  libxcb-sync1 libxcb-xfixes0 libxkbcommon0 libwayland-client0 libwayland-egl1 \
  libxcb-dri3-0 libxcb-present0 libxshmfence1 libdrm2 libllvm15 libsensors5 \
  libxcb-dri2-0 libxcb-glx0 libx11-xcb1 libxxf86vm1
for d in *.deb; do dpkg-deb -x "$d" root; done

export LD_LIBRARY_PATH=~/work/debs/root/usr/lib/x86_64-linux-gnu
export LIBGL_DRIVERS_PATH=$LD_LIBRARY_PATH/dri
export __EGL_VENDOR_LIBRARY_DIRS=~/work/debs/root/usr/share/glvnd/egl_vendor.d
```
Conferir: `ldd squashfs-root/usr/plugins/platforms/libqxcb.so | grep "not found"`.

## 3. Tela virtual

```bash
Xvfb :77 -screen 0 1280x720x24 &
export DISPLAY=:77
export QT_QPA_PLATFORM=xcb
```
No WSL com WSLg dá para pular: a janela aparece no Windows.

## 4. Configuração (`~/.config/PCSX2/inis/PCSX2.ini`)

1. Gerar o .ini completo: `squashfs-root/usr/bin/pcsx2-qt -testconfig`.
   **Importante:** com .ini incompleto o PCSX2 abre o assistente inicial
   numa janela invisível e trava depois de "Loading config".
2. Copiar a BIOS `scph39001.bin` (+ `.MEC`/`.NVM`) para `~/.config/PCSX2/bios/`.
3. Ajustar:

| Chave | Valor | Por quê |
|---|---|---|
| `SetupWizardIncomplete` | `false` | sem assistente |
| `BIOS` (seção Filenames) | `scph39001.bin` | BIOS americana real |
| `HostFs` | `true` | o ELF lê `host:SLUS_004.02` e o disco da pasta dele |
| `Renderer` | `13` | renderizador por software (sem GPU) |
| `Backend` (SPU2/Output) | `Null` | sem áudio |
| `EnableEEConsole` / `EnableIOPConsole` | `true` | printf do port vai para o log |

## 5. Rodar o ELF e capturar

```bash
cd ~/work/squashfs-root
./usr/bin/pcsx2-qt -earlyconsolelog -logfile ~/work/emu.log -batch -fastboot \
    -- "/caminho/tekken 3 ps2/Codigo/ps2/tekken3.elf" &
sleep 60
LD_LIBRARY_PATH= import -window root tela.png
kill -9 %1
grep -A80 "Tekken 3 PS2" ~/work/emu.log
```

Pegadinhas:
- Passe o ELF **depois de `--`**. Com `-elf`, o PCSX2 Linux ignorava o ELF e
  abria a tela do memory card da BIOS.
- `-logfile` grava o log do console do EE (linhas `bios:`, `cdrom:`,
  `io: 1a`, `PARADA:` e o texto do próprio jogo).
- `host:` aponta para a pasta do ELF.
- `import` com `LD_LIBRARY_PATH=` vazio evita conflito com as libs do item 2.
- Em ambientes que matam processos em segundo plano ao fim de cada comando,
  suba Xvfb + PCSX2, espere, capture e feche tudo no mesmo comando (é o que
  o script faz).

## 6. Script

```bash
bash ps2/tools/rodar_pcsx2_linux.sh 60   # segundos rodando
```
Saídas: `ps2/docs/ultimo_log_pcsx2.txt` (log) e `ps2/build/tela_pcsx2.png`
(print). Caminhos configuráveis: `PCSX2_DIR` (padrão `~/work/squashfs-root`)
e `LIBS_DIR` (padrão `~/work/debs/root/usr/lib/x86_64-linux-gnu`; com as libs
instaladas pelo apt, use `LIBS_DIR=/usr/lib/x86_64-linux-gnu`).

Desempenho observado: 60 FPS no Ryzen 5 5600G mesmo com renderização por
software.

## Sem limitador de velocidade (testes longos)

Em `~/.config/PCSX2/inis/PCSX2.ini`, seção `[Framerate]`:
`NominalScalar = 0` (velocidade ilimitada). O jogo mede o tempo pelo COUNT do
EE, então se comporta igual — só termina ~3,5× mais rápido no relógio. Os
dumps de VRAM (`DEBUG_VRAM=1`) usam tempo do EE (30/200/300 s).
Para chegar à luta: `make AUTO_START=1 DEBUG_VRAM=1` e rodar ~110 s.
