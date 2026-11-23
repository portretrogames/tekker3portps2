# Plano: bibliotecas compartilhadas entre os ports (ps2-port-kit)

Decisão (15/11/2026): **repositório novo** no GitHub (ex.: `ps2-port-kit`),
usado por cada jogo como **submódulo git**. Fazer **depois** de resolver a
lentidão no PS2 real (log do console), para não misturar mudanças.

## Arquitetura (3 camadas; ideias da revisão do ChatGPT incorporadas)
1. **Base PS2** — IOP, USB, arquivos, controle, vídeo, tempo, memória,
   diagnóstico. Serve para qualquer port (inclusive SNES/Mario).
2. **Compatibilidade PS1** (opcional) — GPU/GS, GTE, MDEC, CD, BIOS.
3. **Jogo** — código recompilado, overlays, nomes de arquivos, ajustes.

Regras:
- a biblioteca não conhece Tekken, `TEKKEN3.BIN`, SLUS, caminhos ou offsets
  do jogo; o projeto passa isso por configuração (`jogo.h`);
- preparar o armazenamento é separado de procurar os assets do jogo
  (hoje `iop_usb.c` mistura os dois);
- a biblioteca devolve resultados claros; o programa principal decide como
  reagir a erros;
- manter os dois caminhos: `host:` (PCSX2) e USB (console);
- interfaces pequenas em C, dependências explícitas, nada para recursos
  que ainda não existem;
- headers públicos separados dos internos; documentação de uso, dependências
  e limitações; exemplo mínimo sem assets do Tekken; CHANGELOG; origem e
  licença do código reaproveitado; nada de dump/código recompilado no kit;
- cada extração: compila o ELF, testes existentes + testes do contrato do
  módulo, Tekken usando o módulo, e registro do que foi testado **no host,
  no PCSX2 e no PS2 físico** (PCSX2 não prova o console).

## Situação
- **Etapa 1 FEITA (15/11/2026, kit 0.1.0):** `kit/` com controle,
  armazenamento e diagnóstico; o Tekken usa o kit (`ps2/runtime/jogo.h`
  guarda nomes/versão). Testado: host e PCSX2 (exemplo `kit/exemplo` e
  Tekken com FORCAR_IOP). PS2 físico: pendente.

## Etapas
- **Etapa 1 (baixo risco):** só três módulos, primeiro numa pasta do
  próprio projeto (`kit/`), depois movidos para o repositório:
  - *controle* — iniciar, leitura digital, estado de conexão, falhas;
    interface para 2 portas (sem prometer analógico/vibração);
  - *armazenamento* — IOP/USB, espera da montagem, abrir arquivos, erros
    claros (a procura do `.bin`/SLUS vai para o jogo);
  - *diagnóstico* — mensagens por subsistema, níveis de log, log em
    arquivo (reabre a cada ~2 s), medição básica de tempo, painel.
- **Etapa 2:** GS/GPU, MDEC, GTE, CD, BIOS → camada PS1. Antes, corrigir as
  falhas conhecidas: leituras VRAM→CPU pela sombra, render-to-texture em
  páginas 4/8 bits, sprites espelhados, custo por polígono.
- **Etapa 3:** repositório `ps2-port-kit` como submódulo do Tekken; avaliar
  o Yu-Gi-Oh.

## 1. Base PS2 (qualquer port: Tekken, Yu-Gi-Oh, Mario)
| módulo | origem neste projeto |
|---|---|
| boot USB/OPL (IOP, iomanX, fileXio, usbd, bdm, FAT32/exFAT) | runtime/iop_usb.* |
| log em arquivo no pendrive (reabre a cada ~2 s) | runtime/main.c (ps2_log_gravar) |
| painel de texto na tela (fonte 3x5) | runtime/gs_render.c (desenhar_painel) |
| controle do PS2 (libpad) | runtime/controle_ps2.* |
| vídeo: gsKit em FIELD 640x448, fila UCAB para o GIF | video_gs.c / gs_render.c |
| teste automático PCSX2 Linux + captura | tools/rodar_pcsx2_linux.sh, docs/PCSX2_LINUX.md |
| `make pacote` (APPS/<JOGO>/ + title.cfg do OPL) | Makefile |
| futuro: memory card, intro/créditos padrão | — |

## 2. Runtime PS1 (Tekken, Yu-Gi-Oh e futuros jogos de PS1)
| módulo | arquivos |
|---|---|
| RAM, memória, código sujo | ram.*, fast_memory.h, memory.c, codigo.c |
| I/O: IRQ, timers, DMA, orçamento da GPU | io.* |
| GPU do PS1: software (reserva) + GS | gpu.*, gs_render.* |
| MDEC (vídeos) | mdec.* |
| GTE | gte.* |
| CD-ROM + executável lido do .bin (SYSTEM.CNF) | cdrom.*, parte do main.c |
| BIOS HLE, exceções, eventos | bios_hle.* |
| controle PS1 (SIO) | pad.* |
| interpretador de overlays | interp.* |
| testes de regressão | tests/ |

## 3. O que fica em cada jogo
- código gerado (gerado/), sementes de ponteiros
- `jogo.h`: nome do executável, nomes do .bin, título do OPL, pasta
- ajustes específicos (ex.: ENDX do SPU, se outro jogo precisar diferente)

## Passos (quando for a hora)
1. Criar o repositório e mover os módulos com histórico.
2. Trocar constantes do Tekken por `jogo.h`.
3. Tekken passa a usar o kit como submódulo; testes e PCSX2 iguais.
4. Avaliar o port do Yu-Gi-Oh para usar o kit (ele é PS1, mesma lógica).
