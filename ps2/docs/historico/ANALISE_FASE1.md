# Fase 1 — Análise de viabilidade (14/11/2026)

## 1. Dump

- `Tekken 3 (USA) (Track 1).bin` — MD5 `42c63a4674f569de7b1be95d7332cde2`
  bate com o MD5 esperado em `game.toml` (versão SLUS-00402 correta).
- Track 2 e 3 = faixas de áudio CDDA.

## 2. Recompilação (psxrecomp vendorizado no Tekken3Recompiled)

- Recompilador compilado em Linux (precisou copiar os headers `generated/` do
  rabbitizer do psxrecomp upstream — o repositório do FishB0nes98 não os inclui;
  a cópia em `referencia/` já está corrigida).
- Saída: **45 arquivos C, ~1,7 milhão de linhas**, 2.978 entradas na tabela de
  dispatch, 1.350 funções semente (Ghidra) em `seeds/ghidra_funcs.txt`.
- Também gera o OpenBIOS recompilado (651 funções, sem BIOS da Sony).

## 3. Teste decisivo: o código compila para o EE do PS2?

**SIM.** Todos os 46 arquivos compilaram com `mips64r5900el-ps2-elf-gcc -O2`
sem nenhum erro.

| Configuração | Código no EE |
|---|---|
| Sem contagem de ciclos (`PSX_ENABLE_BLOCK_CYCLES` desligado) | **~3,9 MB** |
| Com contagem de ciclos (modo de precisão do PC) | ~11,5 MB (estimado) |

O PS2 tem 32 MB de RAM no EE, então o código do jogo cabe com folga. A
contagem de ciclos (usada no PC para imitar o timing exato do PS1) deve ficar
**desligada** no PS2: ela triplica o código e o custo de CPU. O jogo vai ser
cadenciado pelo VSync do PS2.

## 4. Fronteira com o hardware: só 26 símbolos

O código gerado chama apenas **26 funções/variáveis externas** (lista em
`runtime_api.txt`), além dos ponteiros `cpu->write_byte/half/word`. Ou seja,
o runtime do PS2 precisa implementar:

| Grupo | Símbolos | Implementação no PS2 |
|---|---|---|
| Memória | `psx_read_half/word`, `psx_cyc_load_byte`, `cpu->write_*`, `psx_cyc_lwc2_read` | RAM do PS1 (2 MB) num array alinhado no EE; caminho rápido inline para RAM, função só para I/O |
| GTE | `gte_execute`, `gte_read/write_data/ctrl`, `gte_precision_store_word` | Portar `gte.cpp` (C++) e depois otimizar as operações quentes (RTPT, NCLIP, MVMVA) com VU0 em modo macro |
| Interrupções/VSync | `psx_check_interrupts_at`, `psx_check_interrupts_dispatch_entry`, `psx_vsync_query_hle_try` | Contadores simples + VSync real do PS2 |
| BIOS | `psx_syscall` | OpenBIOS recompilado (já gerado) |
| Fluxo | `psx_unknown_dispatch`, `psx_restore_state_escape` | Tabela de dispatch + overlays pré-compilados |
| Widescreen | `psx_ws_*` (5 símbolos) | Inicialmente devolver o valor original (4:3); depois ativar widescreen |
| Debug | `g_debug_last_store_pc`, `g_psx_last_fn_entry`, `g_psx_cps_mode`, `dirty_ram_text_native_ok_ranges_from` | Variáveis/stubs vazios |

A GPU, o SPU, o CD e a DMA não são chamados diretamente pelo jogo: eles são
acessados por escrita em endereços de I/O (`0x1F80xxxx`), então entram pelo
caminho de I/O da memória.

## 5. Riscos (em ordem)

1. **GPU → GS** (o maior trabalho). O runtime do PC tem ~6.100 linhas só em
   `gpu.c` + renderizador OpenGL. No PS2 é preciso traduzir as primitivas do PS1
   para o GS (gsKit), incluindo páginas de textura e paletas CLUT de 4/8 bits.
   O GS tem suporte nativo a texturas com paleta (PSMT4/PSMT8), o que ajuda.
2. **Desempenho da CPU.** Cada acesso à memória passa por função. Precisa de
   caminho rápido inline para a RAM. Só dá para saber o FPS real medindo
   (Fase 2).
3. **Overlays.** O Tekken 3 carrega código do disco durante o jogo. No PC isso
   é capturado e compilado na hora; no PS2 não há compilador, então os
   overlays têm que ser **capturados no PC e pré-compilados** dentro do ELF.
4. **GTE** em software no EE pode pesar nas lutas; o VU0 resolve.
5. **Áudio:** SPU do PS1 → SPU2 do PS2 (arquitetura parecida) + faixas CDDA.

## 6. Licença

O psxrecomp usa **PolyForm Noncommercial 1.0.0**. O port precisa ser gratuito
e sem fins comerciais. Doações podem ser zona cinzenta nessa licença — o mais
seguro é confirmar com o autor (mstan) antes de divulgar.

## 7. Observação: Tekken 5

O repositório de referência tem uma ferramenta (`tools/tekken3_ps2_vab.py`)
que extrai sons de um ELF do Tekken 3 que existe dentro do **Tekken 5 do PS2**
(modo Arcade History). Esse ELF pode servir de referência de como a Namco
resolveu áudio/vídeo no PS2, mas é a versão arcade, não a do PS1.

## 8. Próximo passo (Fase 2)

Criar `ps2/runtime/` com: RAM, stubs dos 26 símbolos, OpenBIOS recompilado,
leitura do disco pelo USB, e um contador de quadros. Objetivo: o jogo chegar
na tela de título (mesmo sem imagem) no PCSX2 e medir quantos quadros por
segundo a lógica atinge.
