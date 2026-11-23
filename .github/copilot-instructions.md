# Instruções do projeto — Port do Tekken 3 (PS1) para PS2

Responda sempre em **português do Brasil**.

## Objetivo
Port **nativo** do Tekken 3 (USA, SLUS-00402) para PlayStation 2, rodando
direto no Emotion Engine (EE). **Não é emulador**: não sugira POPStarter,
ePSXe, PCSX ou qualquer emulação de CPU. O código MIPS do PS1 já foi
recompilado para C (psxrecomp) e esse C é compilado para o EE com o ps2sdk.
Só a camada de hardware é reescrita para o PS2.

Teste final: PS2 real pelo OPL, com pendrive em exFAT. Teste rápido: PCSX2.

## Leia antes de qualquer tarefa
1. `ps2/README.md` — estrutura e fases
2. `ps2/docs/VISAO_GERAL.md` — arquitetura, estado, desempenho e pendências
3. `ps2/docs/historico/runtime_api.txt` — os 26 símbolos que o runtime do PS2 deve fornecer

## Estrutura
- `gerado/` — C gerado a partir do dump (SLUS_004.02_full_*.c, dispatch, decls).
  **NUNCA editar à mão** (é regenerado) e **NUNCA publicar** (código do jogo original).
- `referencia/Tekken3Recompiled/` — projeto PC de terceiros (FishB0nes98) com o
  psxrecomp vendorizado. Use como **referência** de como cada peça funciona
  (`psxrecomp/runtime/src/gpu.c`, `gte.cpp`, `spu.c`, `cdrom.c`, `memory.c`...).
  Não modificar; copiar para `ps2/` o que for adaptar.
- `ps2/` — **nosso código** (o único lugar onde escrever código novo).
- `*.bin` / `*.cue` — dump do jogo. **NUNCA publicar.**

## Regras técnicas
- Compilar o código gerado com `-DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1`
  e **sem** `PSX_ENABLE_BLOCK_CYCLES` (a contagem de ciclos triplica o código e
  o custo de CPU). O ritmo do jogo vem do VSync do PS2.
- Toolchain: `mips64r5900el-ps2-elf-gcc` (ps2dev), flags base `-O2 -G0`.
- Memória: RAM do PS1 (2 MB) num array alinhado no EE, com **caminho rápido
  inline** para RAM; função só para I/O (`0x1F80xxxx`: GPU, SPU, CD, DMA, timers).
- Mapeamento de hardware:
  - GPU do PS1 → GS via gsKit (texturas com paleta PSMT4/PSMT8 para CLUT)
  - GTE → portar `gte.cpp`; depois otimizar operações quentes com VU0 (modo macro)
  - SPU → SPU2 (audsrv/IOP); faixas 2 e 3 do disco são CDDA
  - CD → leitura de arquivos pelo USB (OPL)
  - Controle → libpad
  - BIOS → OpenBIOS recompilado em `referencia/.../psxrecomp/generated/`
- Overlays carregados em tempo de execução precisam ser **pré-compilados** no ELF
  (no PS2 não há compilador em tempo de execução).
- Código novo em C, bem comentado em português, um módulo por arquivo
  (ex.: `ps2/runtime/memory.c`, `gpu_gs.c`, `gte.c`, `spu2.c`, `cd_usb.c`).

## Onde trabalhar
Trabalhe DIRETO em `~/PROJETOS/tekken 3 ps2/Codigo` (o repositório git; nunca
em worktree separado). As outras pastas do projeto (`ROM/` com o dump,
`ELF OU ISO TESTE/`, `Github/`, `INSTRUÇÕES IA/`, `BACKUPS/`) estão explicadas
em `../INSTRUÇÕES IA/LEIA-PRIMEIRO.md`. O `.bin` em `ps2/` é um atalho para
`../ROM`.
Faça commit ao fim de cada etapa. Antes do commit, `git status` não pode
mostrar nada de `gerado/`, `referencia/`, `build/`, `*.elf`, `*.bin`, `*.cue`,
`SLUS_*` ou `docs/ultimo_log_pcsx2.txt`.

## Estado atual do runtime (ps2/runtime) — leia antes de mexer
- `main.c`: carrega o SLUS_004.02 (PS-X EXE), abre o disco e roda o **laço de
  despacho CPS**: cada função gerada termina gravando o próximo endereço em
  `cpu->pc`; o laço chama `psx_dispatch_game_compiled`, depois
  `ps2_bios_call`, depois `ps2_interp_run`. Interrupções voltam ao laço por
  `longjmp(g_ps2_volta_ao_laco)`.
- `ram.h`/`ram.c`/`fast_memory.h`: RAM 2 MB com acesso direto/inline
  (fast_memory.h é incluído só nos arquivos de gerado/ via `-include`).
- `memory.c`: scratchpad, cache control, encaminha I/O para `io.c`;
  endereço não implementado PARA com log (de propósito).
- `io.c`: I_STAT/I_MASK, timers (IRQ), DMA (canal 6 OTC real, canal 3 = CD,
  demais terminam na hora), GPUSTAT sempre pronto, log do 1º acesso a cada
  registrador (`io: 1a leitura/escrita`).
- `cdrom.c`: controlador de CD completo o suficiente para a libcd; lê o .bin.
- `pad.c`: controle digital na porta 1 (`ps2_pad_botoes`, ativo em 0).
- `bios_hle.c`: BIOS do PS1 em C (A0/B0/C0), eventos, SysEnqIntRP, root
  counters, exceções (VBlank 59,94 Hz), memory card ausente.
- `interp.c`: interpretador R3000A para overlays (código carregado do CD).
- `stubs.c`: GTE ainda vazio (só guarda registradores), widescreen 4:3.

## Fases
1. Base e viabilidade — concluída
2. Runtime mínimo — concluída: o jogo lê o disco, roda overlays e chega no
   vídeo de abertura (para no MDEC). Ver `ps2/docs/FASE2_ETAPA4.md`.
3. **Vídeo** ← fase atual: MDEC provisório (pular FMV) e GPU do PS1 → GS
4. GTE no VU0, áudio (SPU2, CDDA, XA), controle do PS2, pré-compilar overlays
5. Widescreen, tradução PT-BR, mods

## Como testar no Windows
1. Compile no WSL: em `ps2/`, `make test && make -j4`
   (`export PS2DEV=~/ps2dev/ps2dev PS2SDK=$PS2DEV/ps2sdk PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin`).
2. Rode `ps2\rodar_pcsx2.bat` (abre o PCSX2 com o ELF; ao fechar copia o
   log para `ps2\docs\ultimo_log_pcsx2.txt`). O `SLUS_004.02` fica em `ps2\`.
   Alternativa automática no WSL (sem janela): `bash ps2/tools/rodar_pcsx2_linux.sh 60`
   — guia em `ps2/docs/PCSX2_LINUX.md`.
3. Leia no log: mensagens do próprio jogo (texto que ele imprime), linhas
   `bios:`, `cdrom:`, `io: 1a ...`, `vivo:` e `PARADA:`.

## Como reproduzir a base (WSL/Linux)
`bash ps2/tools/gerar_e_compilar_ee.sh /caminho/do/ps2dev`
Esperado: ~3,9 MB de código no total e a lista dos 26 símbolos.

## Licença e créditos
O psxrecomp usa **PolyForm Noncommercial 1.0.0**: o port deve ser gratuito e
sem fins comerciais. Créditos: PSXRecomp (mstan), Tekken3Recompiled
(FishB0nes98), ps2sdk/ps2dev, e as IAs usadas (Claude, Gemini, ChatGPT, Copilot).
