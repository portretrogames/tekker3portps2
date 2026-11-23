# Fase 3 — Etapa 1b: código sobrescrito por overlay (correção)

## Problema (teste do Copilot, parada em 0x800B74B0)

O recompilador gerou "funções" para regiões que no executável original são
**zeros** (áreas reservadas para overlays, ex.: 0x800B0548-0x800B4D48 e
0x800B74B0). Em tempo de execução o jogo carrega código do `TEKKEN3.BNS`
nessas regiões (ex.: 9 setores, lba 251471-251479 -> 0x800B0548), mas o
despacho continuava executando a versão compilada antiga (deslize de `nop`),
porque `dirty_ram_text_native_ok_ranges_from` aceitava sempre.

## Correção

- `runtime/codigo.c`: guarda uma cópia do executável original (1,1 MB).
- `ram.h`: toda escrita na RAM dentro da área do executável marca um bit
  "sujo" a cada 64 bytes (`ps2_cod_marcar`, custo: 1 subtração + 1 comparação).
- `dirty_ram_text_native_ok_ranges_from`: se algum grânulo da função está
  sujo, compara com o original; igual -> limpa o bit e usa o código
  compilado; diferente -> **não** usa (o laço manda para o interpretador).
- `stubs.c` / `main.c`: `psx_unknown_dispatch` (função gerada a partir de
  dados) marca o endereço para ser interpretado em vez de parar.
- `interp.c`: só sai para código compilado que ainda é válido
  (`psx_game_text_native_ok`).
- `cdrom.c`: log das transferências DMA3 (lba -> endereço) para depuração.

## Resultado (PCSX2 Linux, 90 s)

Sem paradas. O jogo passa pelo vídeo de abertura (MDEC provisório, imagem
preta), lê o FMV do `TEKKEN3.XAS`, executa ~20 milhões de instruções de
overlay e começa a usar o **GTE** (cálculo 3D) — provavelmente a tela de
título/demonstração. `VSync: timeout` aparece em vários momentos (o jogo
fica lento enquanto roda código interpretado).

## Próximo

GTE (cálculos 3D) e GPU → GS para ver imagem.
