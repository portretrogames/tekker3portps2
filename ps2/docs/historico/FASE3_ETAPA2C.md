# Fase 3 — Etapa 2c: GPU mais rápida e controle reconhecido (menu principal)

## Resultado

- O jogo chega ao **menu principal** (Arcade / VS / Team Battle / Option /
  Practice) com START apertado na tela de título — ver
  `FASE3_MENU_PRINCIPAL.png` (captura da VRAM no PCSX2).
- Tela do logo/título: **7,7 → ~24 quadros/s** no PCSX2. Telas simples a 60.

## GPU (runtime/gpu.c)

- `triangulo()` incremental: funções de borda em inteiros (64 bits) e
  gradientes 16.16 para cor (gouraud) e UV; o atributo exato é recalculado no
  1º pixel de cada linha. Antes eram divisões de 64 bits por pixel.
  Comparado com a versão anterior: 19 de 524288 pixels diferem (arredondamento
  de UV); retângulos idênticos.
- `retangulo()`: recorte uma vez; preenchimento sólido direto;
  texturizado sem janela de textura e sem espelhamento vai para
  `ret_texturizado()`, que tem laços "brutos" por profundidade (4/8/15 bits,
  `prof >= 2` cobre o valor reservado 3) quando não há modulação, semi nem
  máscara. Caso geral continua pixel a pixel.
- `pixel_final()` (sem recorte) separado de `desenhar_pixel()`.
- Contadores de perfil (pixels por caminho, ciclos do EE na GPU) acumulados em
  64 bits — o COUNT do EE dá a volta a cada ~14,6 s.

## Controle (runtime/pad.c) — causa da "animação congelada"

A tela parada não era travamento: era a tela de título esperando START, e o
controle **nunca era detectado**. O driver do jogo fala direto com o SIO:

1. escreve TX = 0x01;
2. **limpa I_STAT bit 7** logo em seguida;
3. espera a IRQ 7 (ACK) em I_STAT, com timeout pelo timer 2.

O runtime levantava a IRQ dentro da própria escrita do TX, então o passo 2
apagava o ACK e o jogo concluía "sem controle" na porta 1. Agora o ACK é
agendado e entregue ~100 ciclos PS1 depois (`ps2_pad_tick()`, chamado na
leitura de I_STAT, na leitura de JOY_STAT e em `ps2_irq_poll`). O bit 7 de
JOY_STAT (nível do /ACK) fica 0 — no hardware é só um pulso.

Com isso o jogo lê o controle digital (0x42), sonda 0x43/0x45 (DualShock) e
aceita o START.

## CD (runtime/cdrom.c)

- Se o jogo não consome um setor, um novo é entregue após 8 períodos (evita
  ficar parado esperando INT1).

## Testes

- `make AUTO_START=1` aperta START sozinho (60 VBlanks a cada 5 s depois de
  40 s) — útil no PCSX2 headless, que não tem controle.
- `make test`: todos OK.

## Próximo

- Ler o controle do PS2 de verdade (libpad → `ps2_pad_botoes`).
- MDEC real + exibição 24 bits (FMVs).
- Entrar em uma luta (Arcade) e medir desempenho com personagens 3D.

## Controle do PS2 (runtime/controle_ps2.c)

- Carrega `rom0:SIO2MAN` e `rom0:PADMAN`, abre a porta 1 com libpad e, a cada
  VBlank, copia `padButtonStatus.btns` para `ps2_pad_botoes[0]` (mesmo layout
  do PS1, ativo em 0). Se algo falhar, o jogo segue sem controle.
- No PCSX2: estado 6 (estável) ~0,2 s depois do boot.
- `AUTO_START=1` agora soma o START ao que o controle real informa.
- Observação: na tela de título o START nem sempre é aceito na 1ª vez
  (nos testes entrou no menu depois de 3 a 7 toques de 1 s). A investigar —
  pode ser a fase da animação do título ou o ritmo de ~24 quadros/s.
- Ainda é controle digital para o jogo (sem analógico/vibração).
