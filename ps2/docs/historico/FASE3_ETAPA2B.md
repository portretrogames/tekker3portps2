# Fase 3, etapa 2b — investigação da tela preta

## Resultado

A área de exibição **não permanece inteiramente preta**: nos dumps aparecem
541 pixels não pretos, formando o texto “NAMCO PRESENTS”, muito escuro. A
intensidade máxima sobe de 8/255 para 16/255 entre os dumps de 40 e 80 s.
Isso demonstra desenho na área visível e mudança compatível com um fade
lento, mas **não prova** que a lentidão seja a única causa nem que o jogo
alcance o logo ou o menu. Não há evidência de logo/menu nos dumps até 80 s
nem registro que confirme sua chegada até o encerramento aos 120 s.

| Dump | Pixels RGB não pretos em `(0,0)-(367,479)` | Valor RGB máximo |
|---|---:|---:|
| [20 s](FASE3_ETAPA2B_20.png) | 0 | 0 |
| [40 s](FASE3_ETAPA2B_40.png) | 541 | 8 |
| [80 s](FASE3_ETAPA2B_80.png) | 541 | 16 |

Entre 20 e 40 s mudaram 541 pixels e entre 40 e 80 s também. Os pixels não
pretos estão dentro de `(118,241)-(248,254)`. A comparação considera apenas
RGB, não o bit 15 de máscara. A última amostra periódica anterior aos
primeiros comandos de desenho mostrava a exibição desativada.

## Execução e medições

PCSX2 Linux 2.9.179 no WSLg, renderizador software, HostFs, ELF compilado com
`DEBUG_GPU=1 DEBUG_VRAM=1`, interrompido pelo limite de 120 s. Foi usado o
procedimento manual do [guia Linux](PCSX2_LINUX.md): o script depende de
Xvfb, indisponível neste ambiente. Os dumps foram salvos após 20.058, 40.018
e 80.055 s medidos pelo COUNT do EE a partir do primeiro VBlank; os tempos
no log do PCSX2 foram 23.788, 43.740 e 83.774 s, respectivamente.

De `vblank: 1200` em 34,445 s a `vblank: 2100` em 100,675 s houve cerca
de **13,6 VBlanks/s** na execução instrumentada. Isso não é uma medição de
FPS do jogo nem necessariamente do build sem diagnóstico. A atualização
periódica do vídeo passou de 62.954 para 145.058 palavras GP0 entre 34,441
e 113,906 s. Não houve `PARADA:` nem `MDEC_in_sync timeout` no log.

O último `vivo:` registrado foi `pc=8008c328`, com 5.242.880 despachos e
8 instruções interpretadas; não há registro posterior desse contador, portanto
ele **não identifica** o PC no fim da execução. Os últimos registros
`io: 1a` foram escrita no timer, escrita em GP0 e leitura de timer; os
últimos `bios:` foram `_bu_init`, `SysDeqIntRP` e `SysEnqIntRP`. Esses logs
registram primeiras ocorrências, não necessariamente as últimas operações
executadas.

Trecho do log, com vértices já deslocados:

```text
[19.8779] gpu dbg: GP1 05 valor=000000
[19.8789] gpu dbg: GP1 06 valor=c60260
[19.8795] gpu dbg: GP1 07 valor=040010
[19.8802] gpu dbg: GP1 08 valor=000064
[20.6956] gpu dbg: #3 op=60 modo=0200 clut=0000 area=(0,20)-(367,467) fora=0 grande=0 pixels=164864 v0=(0,0) rgb=000000 uv=(0,0) v1=(367,479) rgb=000000 uv=(0,0)
[20.6966] gpu dbg: #4 op=65 modo=022d clut=7e16 area=(0,20)-(367,467) fora=0 grande=0 pixels=22 v0=(243,240) rgb=000000 uv=(224,208) v1=(252,255) rgb=000000 uv=(224,208)
[20.7209] gpu dbg: #18 op=62 modo=0240 clut=0000 area=(0,20)-(367,467) fora=0 grande=0 pixels=2430 v0=(117,240) rgb=ffffff uv=(0,0) v1=(251,257) rgb=ffffff uv=(0,0)
```

O primeiro retângulo limpa de preto; o sprite texturizado e o retângulo
semitransparente escrevem dentro da área exibida. `pixels` conta escritas
aceitas, não necessariamente pixels únicos ou uma mudança de cor. `fora` e
`grande` descrevem a caixa envolvente; `pixels=0` também pode resultar de
textura transparente, máscara ou geometria degenerada. O limite de 300
comandos abrange apenas o começo do desenho, não toda a execução.

## Verificações e correção

- E5 e vértices usam sinal de 11 bits; o offset é aplicado antes do recorte.
  O preenchimento arredonda X e largura a 16 e ignora offset e área.
- E3/E4 eram limitados a **9 bits em Y**. Foram alterados para preservar os
  10 bits solicitados; a escrita ainda é recortada à VRAM física de 512
  linhas. O teste cobre uma área iniciada em Y=512 e o valor lido por GP1.
  Não há evidência de que esse caso tenha causado a tela escura observada.
  O [PSX-SPX](https://psx-spx.consoledev.net/graphicsprocessingunitgpu/)
  distingue GPUs v0 (9 bits) e v2 (10 bits); este runtime não seleciona
  revisão da GPU, portanto a compatibilidade com v0 permanece pendente.
- O limite de tamanho era aplicado apenas a cada triângulo; um quad com
  caixa maior que 1023×511 podia desenhar parcialmente. Agora ele é
  descartado como polígono, conforme o requisito desta etapa; há teste de
  regressão. Não há evidência de que tenha causado a tela escura.
  O PSX-SPX descreve quads como dois triângulos e execução podendo começar
  antes do quarto vértice; a fidelidade do descarte integral precisa de
  confirmação específica em hardware.
- DMA2 encerra a lista no bit 23 e nós sem palavras apenas avançam.
- Semitransparência e máscara têm testes existentes; os registros mostram
  escritas de sprites e de retângulo semitransparente no framebuffer.
- `make test` passou, inclusive os testes `revisao_*`. O ELF foi compilado
  com e sem as flags, verificando a recompilação ao alterná-las.

## Reproduzir

Com ps2dev e Pillow instalados, a partir de `ps2/`:

```sh
make -j4 DEBUG_GPU=1 DEBUG_VRAM=1
# Executar o ELF por pelo menos 120 s, conforme PCSX2_LINUX.md.
python3 tools/vram_png.py
```

Os dumps `vram_dump_20.bin`, `vram_dump_40.bin` e `vram_dump_80.bin` ficam
ao lado do ELF via HostFs e são ignorados pelo Git. O conversor valida o
tamanho, salva PNGs em `build/` e também aceita caminhos explícitos; para
o dump antigo, `python3 tools/vram_png.py vram_dump.bin` produz `vram.png`.
`make` sem as opções volta a compilar sem instrumentação.

## Revisão e conclusão (Claude, 14/11/2026)

Com o brilho realçado, os pixels do dump de 80 s são o texto original
**"NAMCO PRESENTS."** ([FASE3_ETAPA2B_namco_realce.png](FASE3_ETAPA2B_namco_realce.png)):
a GPU desenha corretamente. A tela "preta" era só lentidão.

### Perfil

Medição com o COUNT do EE (log `perfil:` a cada 2 s):

| Antes | GPU desenho | cópia da tela |
|---|---:|---:|
| fade do NAMCO PRESENTS | **95%** | 3% |

O jogo manda poucos comandos (~76 palavras/quadro), mas cada quadro limpa a
tela inteira com um retângulo de 368x480 (164.864 pixels), e cada pixel
passava por `desenhar_pixel` (recorte, textura, transparência e máscara
conferidos por pixel).

### Correção

`retangulo()` agora recorta uma vez contra a área de desenho/VRAM e, para
retângulo de cor sólida sem textura nem transparência, preenche linha a
linha (respeitando o bit de máscara). Os outros retângulos também deixaram
de conferir a área por pixel.

### Resultado (PCSX2, 90 s)

- O jogo passa do "NAMCO PRESENTS", toca o **vídeo de abertura** (a exibição
  muda para 256x240 em 24 bits — modo do MDEC, ainda preto aqui) e volta para
  368x480 com muito mais na VRAM (GP0: de ~145 mil para ~1,44 milhão de
  palavras em 90 s).
- **Primeira imagem na tela**: animação do logo do Tekken 3
  ([PRIMEIRA_IMAGEM.png](PRIMEIRA_IMAGEM.png)).
- Na cena do logo a GPU volta a ocupar ~95% do tempo: agora são os
  **triângulos** (cinco interpolações com divisão de 64 bits por pixel, como
  apontou a revisão técnica). Próximo alvo de desempenho: rasterização de
  triângulos por linha (incremental) e, depois, o GS em hardware.
