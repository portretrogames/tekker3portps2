# Tekken 3 — port nativo para PlayStation 2

Port do **Tekken 3 (PS1, USA, SLUS-00402)** para o **PS2**, sem emulação: o código MIPS
do PS1 é recompilado estaticamente para C (PSXRecomp) e compilado para o Emotion Engine
com o ps2sdk. O hardware do PS1 (GPU→GS, GTE, MDEC, CD, SPU, BIOS, IRQ/DMA) é refeito no
runtime deste repositório.

**Página do port, com o pacote para baixar e como jogar:**
https://portretrogames.github.io/tekker3portps2/

Projeto gratuito e sem fins comerciais, da Retrogames Patos de Minas.

## Duas versões do código

| Ramo | Para quê |
|---|---|
| **`main`** (padrão) | **Versão limpa**, para jogar: só o que o jogo usa, sem log nem painel. |
| **`depuracao`** | **Versão completa**, para testes: log no pendrive e pela rede, painel de desempenho (L3+R3), contadores do processador e as ferramentas de análise. Parada na V60. |

Cada release traz os dois ELFs: o pacote normal e o de **depuração** (grava o
`tekken3_log.txt` ao lado do ELF, para anexar quando for reportar um erro).

## Achou um erro?

[Reportar erro](https://github.com/portretrogames/tekker3portps2/issues/new?template=erro.yml):
o formulário pede a versão, como o jogo foi aberto e o que aconteceu, e aceita fotos,
vídeos e o log arrastados para a caixa.

## O que NÃO está aqui

Este repositório tem só o código do port. **O disco do jogo não é distribuído:**

- a imagem do disco (`*.bin` / `*.cue`): use a sua, do seu disco original;
- o C gerado a partir do executável do jogo (`gerado/`) e o `SLUS_004.02`;
- executáveis compilados (`*.elf`) e logs (os ELFs ficam só nas Releases);
- `referencia/` (PSXRecomp e headers de terceiros).

O `.gitignore` bloqueia esses arquivos.

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `ps2/runtime/` | runtime do port (PS1 → PS2): GPU→GS, GTE, MDEC, CD, SPU, BIOS HLE, IRQ/DMA, memory card |
| `ps2/tools/` | transformação do código gerado, tradutor de rotinas para C nativo, empacotamento |
| `ps2/tests/` | testes no PC (`make test`) |
| `ps2/docs/` | documentação (PS2 real/OPL, histórico das versões) |
| `kit/` | **ps2-port-kit**: bibliotecas reutilizáveis (controle, armazenamento, som, memory card) |
| `site/` | a página do port no GitHub Pages |

Compilação e testes: [`ps2/README.md`](ps2/README.md). Rodar no PS2:
[`ps2/docs/PS2_REAL.md`](ps2/docs/PS2_REAL.md).

## Créditos

- **PSXRecomp** — mstan (licença PolyForm Noncommercial 1.0.0)
- **Tekken3Recompiled** — FishB0nes98 (mapas de funções e patches do Tekken 3)
- **ps2dev / PS2SDK / gsKit** e o **Open PS2 Loader**

*Tekken 3* © Bandai Namco Entertainment. Projeto de fãs, sem ligação com a Bandai Namco.
