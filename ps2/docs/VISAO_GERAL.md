# Tekken 3 PS1 → PS2 — visão geral para avaliação

Documento para quem vai **avaliar o código** (pessoa ou outra IA). Resume a
arquitetura, o estado atual, os números medidos e as dúvidas em aberto.
Atualizado em 15/11/2026 (versão do painel: **V17**, kit 0.1.0).

## 1. O que é

Port **nativo** do Tekken 3 (USA, SLUS-00402) para o PlayStation 2 — não é
emulador. O executável MIPS do PS1 foi **recompilado estaticamente para C**
pelo psxrecomp (projeto de terceiros); esse C é compilado para o Emotion
Engine (EE) com o ps2sdk. O hardware do PS1 é reimplementado em C no
runtime deste repositório. Testes: PCSX2 (Linux, sem tela) e **PS2 real**
via OPL com pendrive exFAT (log gravado no pendrive).

### O que NÃO está neste pacote (de propósito)
- dump do jogo (`.bin/.cue`) e o executável `SLUS_004.02`;
- `gerado/` — o C gerado (1,7 milhão de linhas, ~50 MB): é derivado do jogo
  original, não pode ser distribuído;
- `referencia/` — psxrecomp e Tekken3Recompiled (terceiros, ~200 MB). O build
  usa só os headers de `referencia/Tekken3Recompiled/psxrecomp/runtime/include`
  (ex.: `cpu_state.h` com `CPUState { uint32_t gpr[32]; uint32_t pc; ... }`).

Sem esses itens o ELF não compila; o pacote serve para **ler e avaliar** o
runtime, o kit, os testes e as decisões.

## 2. Forma do código gerado (descrição)

- ~121 mil instruções do PS1 → 45 arquivos `SLUS_004.02_full_NN.c` com
  funções `void func_8XXXXXXX(CPUState* cpu)` (texto EE ~8,4 MB).
- Cada instrução vira C sobre `cpu->gpr[]`; loads/stores chamam
  `psx_read_word`/`cpu->write_word`, que o port troca por versões inline
  (`ps2/runtime/fast_memory.h`).
- Todo desvio chama `psx_check_interrupts_at(cpu, alvo)` (o port reduziu a
  um decremento; a checagem real roda a cada 256 desvios).
- **Modelo CPS:** `jal`/`jr $ra` não são chamadas C; a função grava
  `cpu->pc` e retorna ao laço de despacho (`main.c`), que procura a função
  numa tabela (22.325 entradas), valida se o código na RAM ainda é o
  original (overlays) e chama. ~275 mil despachos/s na luta.
- Código que não foi recompilado (overlays carregados do CD, p.ex. o
  tocador de vídeo da abertura) roda num **interpretador** (`interp.c`). Rotinas
  folha pesadas de overlay podem ir para C nativo (`tools/nativas_overlay.txt`,
  V48: o decodificador VLC do vídeo); o interpretador só usa a versão nativa se
  a RAM tiver exatamente o código traduzido.
- O build compila uma **cópia transformada** (`ps2/tools/transformar_gerado.py`
  → `build/r/`): `restrict` no ponteiro da CPU e cache na frente da busca
  do despacho. `gerado/` nunca é editado.

## 3. Estrutura do repositório

| caminho | conteúdo |
|---|---|
| `ps2/runtime/` | runtime PS1→PS2 (≈5.700 linhas C) |
| `ps2/runtime/jogo.h` | o que é só do Tekken: nomes de arquivo, pasta, versão |
| `kit/` | **ps2-port-kit**: controle, armazenamento (USB/host), diagnóstico (log) — reutilizável |
| `ps2/tests/` | testes no PC (`make test`, gcc + ASan/UBSan em parte) |
| `ps2/tools/` | geração do código, teste no PCSX2 Linux, transformação do gerado |
| `ps2/docs/` | `PS2_REAL.md`, `PCSX2_LINUX.md`, `PLANO_KIT.md`, este arquivo; `historico/` |

### Módulos do runtime
| arquivo | papel |
|---|---|
| `main.c` | boot (kit), loader do SLUS (arquivo ou de dentro do .bin via ISO9660/SYSTEM.CNF), laço de despacho |
| `ram.h`, `fast_memory.h`, `memory.c`, `codigo.c` | RAM do PS1 (2 MB + espelhos), scratchpad, I/O, detecção de código sobrescrito (grânulos de 64 B) |
| `io.c` | IRQ, timers, DMA (GPU listas encadeadas, OTC, CD, MDEC), orçamento de GPU, pulo de redesenho, detecção de listas em ciclo |
| `bios_hle.c` | BIOS em C (A0/B0/C0, eventos, exceções, VBlank entregue ao jogo) |
| `gpu.c` | GPU do PS1 (comandos GP0/GP1; sombra da VRAM em software) |
| `gs_render.c`, `video_gs.c` | desenho no **GS** do PS2 (VRAM do PS1 no GS, PSMCT16, CLUT, blending), apresentação, painel, diagnóstico por VBlank |
| `gte.c` | GTE (coprocessador geométrico) em C |
| `mdec.c` | decodificador de vídeo (RLE, IDCT, YUV) |
| `cdrom.c` | controlador do CD; lê o `.bin` com **leitura antecipada assíncrona** (fileXio NOWAIT, 2 buffers de 32 setores) |
| `pad.c` | protocolo SIO do controle do PS1 (alimentado pelo kit) |
| `interp.c` | interpretador MIPS para overlays |
| `nativo_overlay.h` | tabela das rotinas de overlay em C nativo (usada por `interp.c` e pelo `build/r/nativo.c`) |
| `stubs.c` | ligações com o código gerado (syscall, desvios desconhecidos, amostragem de perfil) |
| `trace_fmv.c` | rastreio do vídeo (MDEC, DMA, anel do CD) no log, só com `make TRACE_FMV=1` |

## 4. Estado atual

Funciona no PS2 real: boot pelo pendrive, logos, menus, seleção, controle,
lutas completas (rounds, vitória/derrota), estabilidade de 12+ min.
**Problemas abertos:**
1. **Velocidade da luta no console** — o jogo inteiro (relógio e animação)
   anda devagar. Medido na V14 pelo vídeo do usuário: ~1/4 da velocidade.
   V15–V17 trouxeram ganhos grandes no PCSX2; falta medir no console.
2. ~~Abertura (FMV) pula~~ — resolvido na V48: era o DICR escrito em 8 bits pelo
   libcd (ver `ANALISE_V42_V43.md`, V48); no PCSX2 o vídeo toca inteiro a ~20
   quadros/s com a música em tempo real.
3. Som: música XA desde a V47; efeitos/vozes (SPU) e CDDA ainda mudos. Sem memory
   card, sem analógico.

## 5. Histórico de desempenho (console)

| versão | gargalo encontrado | correção |
|---|---|---|
| V8 | `poll` 75% do EE (trabalho do runtime por VBlank) | medição por partes |
| V9 | **CD 66–71%**: `fseek`+`fread` por setor no `.bin` via USB; travava aos 74 s (libcd esperando DRQ) | blocos de 32 setores; XA não apaga setor de dados; BFRD pendente |
| V10 | CD ainda 41%: cada `fread` de 75 KB parava o EE ~150 ms | leitura **assíncrona** (V11) |
| V11 | ~7 min 30 s: listas do DMA2 em **ciclo** reenviadas 524 mil vezes | validação da cadeia antes de desenhar |
| V13 | gravação do log esperava a leitura do CD (até 141 ms) | log só grava com o fileXio livre |
| V14 | **a mesma OT redesenhada ~10×/quadro** (modo entrelaçado 368x480): GPU 39–46% | pulo do redesenho igual (V15, refeito com segurança na V17) |
| V15–V16 | acessos à RAM viravam chamadas; gravação de PC de depuração a cada store; aliasing do `cpu` | `always_inline`, variável descartada, `restrict` |
| V17 | busca binária no despacho (275 mil/s) | cache direto PC→entrada (99% de acerto) |

Números no PCSX2 (relativos; o PCSX2 subestima o custo do EE real em ~3–4×):
GPU 46%→12%, interrupções 59%→20%, quadros montados (OTs limpas) ~2× maiores.

## 6. O que gostaríamos que fosse avaliado

1. **Desempenho do código gerado no EE**: o modelo CPS (cada call/return
   volta ao laço de despacho), `cpu->gpr[]` em memória, checagem de
   interrupção em todo desvio, tamanho do texto (8,4 MB vs I-cache de
   16 KB). Que mudanças no gerador ou na transformação dariam mais ganho?
2. **Pulo de redesenho** (`io.c`, `dma_gpu`): é seguro? Há casos em que a
   imagem fica errada? (Teste: `ps2/tests/dedup_test.c`; A/B no jogo com
   SELECT+L3.)
3. **GTE** (`gte.c`, 10–17% do EE): caminhos rápidos ou VU0?
4. **CD assíncrono** (`cdrom.c`): corretude das bordas (seek, reset, XA).
5. **Kit** (`kit/`): a divisão base PS2 × camada PS1 × jogo faz sentido para
   reusar em outros ports (Yu-Gi-Oh PS1, Mario SNES)?
6. Riscos de corretude conhecidos (revisão anterior, ainda abertos): parser
   ISO sem limites, MDEC descarta saída com buffer cheio, upload que cruza a
   linha 511, CLUT alterada por desenho, área de desenho Y=512, janela de
   textura 15 bits, `gpu_abort_dma`, COUNT recuado gera IRQ, DICR.

## 7. Como compilar (com os itens que não vão no pacote)

```bash
export PS2DEV=~/ps2dev/ps2dev PS2SDK=$PS2DEV/ps2sdk
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin
cd ps2 && make test && make -j3 && make pacote
```
Teste no PCSX2 Linux: `make FORCAR_IOP=1 AUTO_START=1 && bash tools/rodar_pcsx2_linux.sh 60`.
O log (`tekken3_log.txt`) traz, a cada 120 VBlanks: `csv` (tempo, GPU%,
interrupção%, listas, puladas, polígonos/VB), `prof` (GTE, poll, amostras),
`irq` por origem, `cd` (leituras, espera), `poll` por parte, `perfil:`
(OTs limpas ≈ quadros montados, redesenhos pulados, despachos), `tempo`
(VBlanks reais do PS2 × do PS1).

## 8. Licenças e créditos
- PSXRecomp — mstan (PolyForm Noncommercial 1.0.0)
- Tekken3Recompiled — FishB0nes98
- ps2sdk / gsKit — comunidade ps2dev
- Projeto sem fins comerciais (Retrogames Patos de Minas).
