# Fase 3, etapa 2 — GPU em software e saída GS (experimental)

**A primeira imagem do jogo ainda não foi obtida.** Após 90 segundos no
PCSX2, a tela permaneceu preta e o logo/menu não apareceu. Não há evidência
de `PARADA:`; a causa da falta de progresso permanece em investigação.

## Implementação

- VRAM de 1024×512 pixels de 16 bits; GP0/GPUREAD, GP1 e DMA2 por blocos ou
  lista encadeada. A DMA inválida ou excessiva é registrada e não é dada como
  concluída. A VRAM é preservada por reset GP1.
- Há implementações iniciais de polígonos, linhas, sprites, CLUT de 4/8 bits,
  texturas de 15 bits, mesclagem, janela de textura, área, offset e máscara.
  Elas **não foram integralmente validadas** contra o hardware. Há aproximações
  na rasterização e faltam dithering, comportamento completo de GPUSTAT e
  temporização, entre outros detalhes de hardware.
- `video_gs.c` apresenta no GS, via gsKit/dmaKit, a área configurada por GP1
  em 640×448; 24 bits ainda não são apresentados. A textura completa é
  transferida a cada VBlank, sem otimização.
- O teste sintético de preenchimento vermelho foi exibido no PCSX2. Ele
  demonstra o caminho VRAM→GS, **não** que o jogo esteja desenhando.
- `make test` passou, incluindo preenchimento, triângulo flat, transferências,
  máscara, sprite com CLUT de 4 bits, mistura e DMA2 nos dois modos. O ELF
  também compilou com ps2dev. Outros casos gráficos ainda precisam de testes.

## PCSX2 Linux

PCSX2 2.9.179 no WSLg, renderizador software, HostFs, 90 segundos. A captura
da janela em [FASE3_ETAPA2.png](FASE3_ETAPA2.png) mostra a tela preta. O
contador do PCSX2 mostrou **60 FPS / 60 VPS** na captura; não foi possível
medir FPS do jogo nem inferir que ele esteja avançando a 60 FPS.

O último registro periódico de despacho foi `pc=800833a8`, em aproximadamente
47 segundos, com 5.242.880 despachos e duas instruções interpretadas. Depois
houve registros de VBlank até o encerramento. Os últimos registros de BIOS
foram `TestEvent` e `DeliverEvent`; houve acesso a GPU GP1, GPUREAD e DMA2.
O trecho relevante está em [FASE3_ETAPA2_LOG.txt](FASE3_ETAPA2_LOG.txt).
O log não mostrou `MDEC_in_sync timeout` nem `PARADA:`. Isso não prova que
o jogo tenha passado do vídeo de abertura nesta execução.

O ambiente seguiu o procedimento manual do guia Linux com WSLg; o script
automático depende de Xvfb, indisponível nesta configuração.

## Referências

Documentação PSX-SPX; PSXRecomp de mstan (PolyForm Noncommercial 1.0.0);
Tekken3Recompiled de FishB0nes98; gsKit/dmaKit e ps2sdk/ps2dev.

## Revisão (Claude, 14/11/2026)

Três problemas encontrados e corrigidos depois da GPU do Copilot:

1. **VSync travava no modo 480i.** A `VSync()` da libetc, com a tela
   entrelaçada, espera o bit 31 do GPUSTAT (campo par/ímpar) mudar. Agora
   `ps2_gpu_vblank()` alterna o campo a cada VBlank (bit 31 e bit 13); fora
   do modo entrelaçado o bit alterna a cada leitura.
2. **CD perdia setores** (o jogo relia 251748/251750 para sempre). O port
   processa os setores mais devagar que um PS1; o drive agora só lê o
   próximo setor depois que o jogo reconheceu o INT1 **e** pediu os dados
   (BFRD), com meio período de folga. Cada novo ReadN/ReadS zera esse estado.
3. **Exibição pesada.** Copiar a VRAM a cada VBlank roubava tempo do jogo;
   agora a tela é atualizada a cada 3 VBlanks (20 Hz). Medido: cópia
   ~7,2 ms por quadro, GS ~0,3 ms.

Diagnóstico novo no log (`video: exib=...`, a cada 2 s): estado da exibição,
quanto da VRAM tem cor e quantas palavras o GP0 recebeu. Com
`-DPS2_DEBUG_VRAM` o runtime salva a VRAM em `host:vram_dump.bin`.

### Estado atual

- O jogo carrega o overlay de 140 setores sem erro, liga a exibição em
  **368x480 entrelaçado** e envia comandos à GPU (~9 mil palavras GP0 a cada
  2 s).
- A VRAM aos ~20 s ([FASE3_ETAPA2_vram.png](FASE3_ETAPA2_vram.png)): texturas
  (fontes, sprites) e CLUTs carregadas à direita/embaixo; a **área da tela
  está preta** — ainda nada visível desenhado.
- O VBlank só acontece ~15 vezes/s: o tempo vai para a **renderização em
  software** da GPU no EE (o jogo fica lento). Próximo passo de desempenho:
  desenhar os polígonos direto no GS (hardware) em vez de software.

### Próximos passos

1. Descobrir se a tela preta é uma fase de carregamento/fade lenta ou falha
   no desenho (registrar os primeiros comandos GP0 de desenho: tipo,
   coordenadas, área de desenho e offset).
2. Renderização no GS em hardware (velocidade).
