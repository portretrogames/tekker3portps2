# Tekken 3 — Port nativo para PlayStation 2

Port do **Tekken 3 (USA, SLUS-00402)** do PS1 para o PS2: o código MIPS do PS1
é **recompilado estaticamente para C** (psxrecomp) e compilado para o Emotion
Engine com o ps2sdk. O hardware do PS1 (GPU→GS, GTE, MDEC, CD, BIOS, IRQ/DMA)
é reimplementado no runtime. Código que o jogo carrega do CD em tempo de
execução (overlays) ainda roda num interpretador.

## Estrutura

| Pasta | O que é | Publicar? |
|---|---|---|
| `ps2/runtime/` | runtime do port (PS1 → PS2) + `jogo.h` (nomes/versão do Tekken) | sim |
| `ps2/tests/` | testes no PC (`make test`) | sim |
| `ps2/tools/` | geração do código, teste no PCSX2 Linux, transformação do código gerado | sim |
| `ps2/docs/` | `PS2_REAL.md` (pendrive/OPL), `PCSX2_LINUX.md`, `PLANO_KIT.md`; histórico em `docs/historico/` | sim |
| `kit/` | **ps2-port-kit**: bibliotecas reutilizáveis (controle, armazenamento, diagnóstico) | sim |
| `gerado/` | C gerado do seu dump | **NÃO** (código do jogo) |
| `referencia/Tekken3Recompiled/` | psxrecomp + headers do runtime (terceiros) | não |
| `*.bin`/`*.cue` | seu dump do jogo | **NÃO** |

## Compilar e testar (Linux/WSL)

```bash
export PS2DEV=~/ps2dev/ps2dev PS2SDK=$PS2DEV/ps2sdk
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin
cd ps2 && make test && make -j3 && make pacote
```

- `make pacote` → `build/pacote/APPS/TEKKEN3/` com o ELF sem informações de depuração
  (copie o `.bin` do jogo para lá; ver `docs/PS2_REAL.md`).
- `bash tools/gerar_elf.sh` → compila, gera o pacote e copia o ELF para
  `../../ELF OU ISO TESTE/APPS/TEKKEN3` (e para o pendrive, se estiver montado).
- Rodar no PCSX2 Linux: `bash tools/rodar_pcsx2_linux.sh 60`; no PS2 pela rede (PS2LINK):
  `bash tools/rodar_ps2_rede.sh` (precisam do `.bin` do jogo na pasta `ps2/`).
- Requer gsKit/dmaKit em `$PS2DEV/gsKit`.
- **Duas versões, dois ramos do git:**
  - **`main` (ramo padrão): versão limpa**, para jogar. Sem log, painel, perfis nem opções de
    teste: só o que o jogo usa.
  - **`depuracao`: versão completa**, para testes. Log no pendrive e pela rede, painel
    de desempenho, contadores do R5900, linha do tempo e as ferramentas de análise
    (`git checkout depuracao`; é a V60, tag `V60-completa`). Fica parada na V60: as
    correções feitas na `main` depois dela não entram lá sozinhas.

## Estado (23/11/2026, V60)

Boot pelo pendrive no PS2 real, telas de início da Retrogames (V59), abertura em vídeo,
menus, dois controles e analógico (V59), memory card no memory card do PS2 com ícone 3D
(V59), CD-DA da faixa 2 (V59), luta completa com
o relógio do round a ~99%. Música (XA) e efeitos/vozes (SPU2, ver abaixo). A luta
fica a 60 imagens/s na maior parte dos quadros (~83% pela rede, que atrasa o CD; pelo
pendrive mais); o resto cai para 30 (o quadro cabe em 16,7 ms com pouca folga).
Reverberação não foi feita (o Tekken 3 não usa). Histórico e medidas:
`docs/ANALISE_V42_V43.md`.

## Som (SPU → SPU2)

A SPU do PS1 não é emulada no EE: ela toca na **SPU2 de verdade**, no núcleo 0, que
usa o mesmo ADPCM, o mesmo ADSR e o mesmo formato de volume (24 vozes). A música XA
do CD é decodificada no EE e vai como PCM para o núcleo 1.

**Quem faz o quê**

| Onde | Arquivo | O quê |
|---|---|---|
| EE | `runtime/spu.c` | recebe as escritas do jogo em 0x1F801C00–0x1F801DFF (`io.c` → `som.c`), guarda uma cópia dos 512 KB da RAM de som (DMA 4 e porta 0x1DA8), monta **um pacote por VBlank** (mais um na hora do KON) e lê a resposta (ENDX/ENVX) |
| EE | `kit/src/som.c` | `kit_spu1_*`: envia o pacote por SIF RPC sem esperar e pega a resposta |
| IOP | `kit/iop/audsrv/src/ps1spu.c` | servidor RPC próprio (0x8708850) dentro do audsrv do kit: traduz cada registrador para o núcleo 0 da SPU2 com a libsd e devolve ENDX/ENVX |
| EE | `runtime/som.c` | XA → PCM 48 kHz pelo audsrv (núcleo 1) |

Formato do pacote (palavras de 32 bits): `1<<28 | i<<16 | valor` = registrador
0x1F801C00 + 2·i; `2<<28 | end/8` + tamanho + dados = amostras (blocos de 64 bytes);
`0` = só pedir o ENDX/ENVX. Resposta: ENDX, ENVX das 24 vozes, pacotes, blocos recusados.

**Registradores PS1 → SPU2 (núcleo 0)**

| PS1 | SPU2 | Conversão |
|---|---|---|
| voz +0/+2 volume L/R | `SD_VPARAM_VOLL/VOLR` | direto |
| voz +4 pitch | `SD_VPARAM_PITCH` | × 44100/48000 (3763/4096), limite 0x3FFF |
| voz +6 endereço inicial | `SD_VADDR_SSA` | 0x80000 + valor × 8 (bytes) |
| voz +8/+A ADSR1/ADSR2 | `SD_VPARAM_ADSR1/ADSR2` | direto |
| voz +C ENVX | — | só leitura (vem na resposta) |
| voz +E endereço de repetição | `SD_VADDR_LSAX` | 0x80000 + valor × 8 |
| 1D80/1D82 volume principal | `SD_PARAM_MVOLL/MVOLR` | 0 se o SPUCNT desliga ou cala |
| 1D88/1D8A KON, 1D8C/1D8E KOFF | `SD_SWITCH_KON/KOFF` | juntados no pacote e gravados no fim (KOFF antes de KON), 1 amostra entre duas gravações |
| 1D90/1D92 PMON | `SD_SWITCH_PMON` | voz 0 sem modulação |
| 1D94/1D96 NON | `SD_SWITCH_NON` | direto |
| 1DAA SPUCNT | ATTR do núcleo (bits 8–13, ruído) + MVOL | ATTR escrito direto (o `sceSdSetCoreAttr` estragava o ATTR) |
| 1DA6/1DA8, DMA 4 | `sceSdVoiceTrans` (canal 0) | RAM do PS1 (512 KB) a partir de 0x80000 na SPU2 |
| 1D9C/1D9E ENDX | — | da resposta; voz ligada depois do envio conta como "não terminou" |
| 1D84/1D86, 1D98/1D9A, 1DA2, 1DC0–1DFF (reverb) | — | **guardados, não aplicados** (ver abaixo) |

No começo o IRX zera a RAM do PS1 na SPU2 (no console ela vem com o que o OPL deixou),
liga as vozes na saída seca (VMIXL/R), desliga o efeito do núcleo 0 e põe a saída do
núcleo 0 na entrada externa do núcleo 1 (MMIX).

**Limitações conhecidas**

- **ADSR ~9% mais rápido**: a SPU2 roda a 48 kHz e o PS1 a 44,1 kHz. O pitch é
  corrigido, o envelope não: as taxas do ADSR andam em degraus de ~15–25%, e trocar de
  degrau erraria mais que os 9%.
- **ENVX com um quadro de atraso**: a resposta de um pacote chega no VBlank seguinte. O
  Tekken 3 lê o ENVX das 24 vozes a cada quadro (o ENDX, nunca). Voz recém-ligada: o IOP
  lê o ENVX logo depois do KON (ainda 0), então até a resposta certa a leitura devolve
  0x7FFF (ataque), como o PS1 teria um quadro depois (`spu.c`, `PS2_ENVX_ATAQUE`).
- **Reverberação: não feita.** O Tekken 3 não usa: em todos os logs (console V53, 164
  janelas; PCSX2 da abertura à luta) as escritas em 1DC0–1DFF são 0, o EON fica em 0, o
  bit 7 do SPUCNT nunca liga e o mBASE só é escrito no início (linha `spu2:` do log).
  Para um port que use (ex.: SOTN), o mapeamento conferido no `spu2regs.h` do ps2sdk e no
  `Reverb.cpp` do PCSX2:
  - os 22 endereços de 1DC0–1DFE (dAPF1, dAPF2, mLSAME… mRAPF2) estão na **mesma
    ordem** dos registradores da SPU2 em 0x2E4–0x33A do núcleo (FB_SRC_A, FB_SRC_B,
    IIR_DEST_A0…MIX_DEST_B1; o `spu2regs.h` chama de IIR_SRC_B1 o 17º e de B0 o 18º,
    mas a posição é que vale); valor do PS1 (8 bytes) × 4 = meias-palavras, em hi/lo;
  - os coeficientes (vIIR, vCOMB1–4, vWALL, vAPF1/2, vLIN/vRIN) vão direto, na mesma
    ordem, para 0x774–0x786 do núcleo (IIR_ALPHA … IN_COEF_R);
  - ESA (meias-palavras) = (0x80000 + mBASE × 8) / 2; EEA = 7 no registrador alto (o
    fim vira 0x7FFFF meias-palavras = byte 0xFFFFF, o fim da RAM do PS1 na SPU2);
  - EON → `VMIXEL/VMIXER`; 1D84/1D86 → `EVOLL/EVOLR`; SPUCNT bit 7 → bit 7 do ATTR;
  - o eco fica ~8% mais curto (a área anda a 24 kHz em vez de 22,05 kHz).
- **XA**: pelo EE e pelo núcleo 1 (`som.c`). **CD-DA** (faixas 2 e 3): não feito.

## Créditos

- **PSXRecomp** — mstan (licença PolyForm Noncommercial 1.0.0)
- **Tekken3Recompiled** — FishB0nes98 (mapas de funções, patches do Tekken 3)
- **ps2sdk / ps2dev** — comunidade ps2dev
