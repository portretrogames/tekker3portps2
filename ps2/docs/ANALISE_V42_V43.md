# Lentidão na luta — análise V41d–V43 (17/11/2026)

Medida que vale: cronômetro do round (40 s de jogo; o método do teste mede 39 s
de jogo). PS1 real: 39,12 s.

| Versão | Sem log | Com log |
|---|---|---|
| V33 | 56,5 s (~71%) | — |
| V35/V36 | 48 s (~83%) | — |
| V38 | 47 s (~85%) | — |
| **V42** | **41,06 s (~95%)** | 41,51 s |

## Como o jogo marca o tempo da luta (desmontagem do SLUS_004.02)

- VBlank → VSyncCallback `800299E0` arma o timer 2 (`SetRCnt` alvo 0x13A7, modo
  0x258: repetir, sys/8 ≈ 1,19 ms).
- IRQ do timer 2 → callback `80029784`: `ResetGraph(1)` (`8007C028`); se o
  contador `8009BC5C` == 0 (quadro novo pronto) troca a tela (`PutDispEnv`
  `8007CBD4`) e manda a OT (`DrawOTag` `8007C998`); se != 0 e a trava
  `80095428` == 0, reenvia a mesma OT (redesenho do 480i). No fim:
  `8009BC5C`++, `80095434`++ e desarma o timer (`SetRCnt ...,0xFFFF,0x2000`).
- Laço da luta `800508C4`: passo `8002AE58` (lógica + montagem do desenho); se
  `800295F4` (contador >= 2) roda UM passo de recuperação sem desenho (marca
  `800ADCA4`, testada nas funções de desenho/efeitos). Máximo 2 passos por quadro.
- Fim do quadro `800296C4`: zera `8009BC5C` e espera a próxima IRQ do timer 2
  (`8004CE54` no laço é só a semente aleatória: tempo ocioso).
- Relógio do round `800ADBCC`: passos de 1/60 s que faltam; início
  `(x+2)*600` (`8002A9C8`), -1 por passo (`8003CCDC`), mostra `(t+59)/60`,
  `t < 1` = TIME UP (`8003E478`).

## V42 — corrigido: redesenho completo à toa em metade dos quadros

`io.c`, limpeza da OT (DMA6): o teste `a - sig.madr < n*4+4` invalidava a
referência do "redesenho igual" quando a OT VIZINHA era limpa. As duas OTs do
jogo têm 1024 entradas e são vizinhas (topos 0x0A7584 e 0x0A8584, distância
exatamente n*4 — confirmado com o evento "OTC" da linha do tempo). O DMA6
escreve de MADR para baixo: faixa `[a-(n-1)*4, a]`, logo o certo é `< n*4`.
Efeito: a lista inteira era processada de novo no começo do passo 1 em metade
dos quadros (PCSX2: +4,2 ms nesses quadros).

Console, luta, por 2 s: V39 45 quadros / 90 passos / 68 listas desenhadas;
V42 60 quadros / 119–120 passos / 60 listas; IRQ do timer 2 18% → 13–14%.
Linha do tempo V42 no console (média de 27 quadros): quadro 33,4 ms, espera
ociosa 8–10 ms, passo 1 17–19 ms, passo 2 5,7–7 ms, lista 2,9 ms, VBlank
1,1 ms por quadro.

## Em aberto — IRQ do timer 2 repetida (não é comportamento do PS1)

O port processa a OT inteira dentro da callback (~3 ms no console, mais que o
período de 1,19 ms) com o timer ainda em modo repetir; a callback volta 3–4
vezes por quadro e o contador chega a 4–5 no teste `800295F4` (PS1: no máximo
2). Hoje isso garante o passo de recuperação na luta.

V43 tem a correção (`PS2_T2_FIEL=1`: escrita no modo feita pela própria
callback descarta os cruzamentos), mas DESLIGADA: no PCSX2 o relógio do round
caiu de 99,2% para 93–97%, porque lá o passo 1 termina perto do VBlank seguinte
e, sem as IRQs extras, às vezes o VBlank se perde (o fim do quadro zera o
contador). Só ligar quando a lista sair de dentro da callback (o quadro
começaria ~3 ms antes, como no PS1).

## Por que ainda parece lento

- Na luta o port faz 30 imagens/s com 2 passos de lógica cada (velocidade do
  jogo ~99%). Fontes sobre o PS1 e o Tekken3Recomp no PC indicam ~60
  quadros/s na luta: mesma velocidade, metade da fluidez. Não foi medido no PS1.
- Nos trechos sem passo de recuperação (abertura do round, KO, pose de vitória)
  o port faz 1 passo a cada 2 VBlanks (60 passos por 2 s no log): metade da
  velocidade, se o PS1 roda esses trechos a 60/s.
- Para 60/s o quadro (callback com a lista + passo 1 + VBlank) precisa caber
  em 16,7 ms; no console hoje são ~22 ms.

## V43 — medição sem cronômetro

- Linha `relogio:` a cada ~2 s e `rodada:` no fim de cada round: passos do
  relógio do round por VBlank do PS1 (100% = PS1), tempo real e o do PS1. O
  painel mostra `QUADROS/S`, `JOGO` (últimos 2 s) e `RODADA` (último round).
- Versão sem log: o stdout é descartado (antes cada linha dos relatórios ia
  para o IOP).
- Linha do tempo: ring de 16384 eventos; despeja os quadros > 37 ms ("QUADRO
  LENTO"), um trecho de 1 passo por quadro, e marca relatório e leituras do CD.
- `tools/linha_tempo.py LOG [n]` resume a linha do tempo quadro a quadro.
- `make AUTO_START=1 LOG_BLOCOS=15` + `tools/rodar_pcsx2_linux.sh 230`: o
  PCSX2 entra sozinho numa luta e grava tudo.

## V44 no console (log de 19/11/2026) — timer fiel confirmado

Log em `ELF OU ISO TESTE/resultados_logs/tekken3_log_V44_console.txt` (ELF com log).

| Medida (luta, por 2 s) | V42 | V44 (`PS2_T2_FIEL=1`) |
|---|---|---|
| IRQs do timer 2 vistas pelo jogo | ~250 | **120** (1 por VBlank, como no PS1) |
| Relógio do round (`relogio:`) | — | 99,1–100% |
| Quadros | 59–60 (quase todos 2 VBlanks) | 59–60 (quase todos 2 VBlanks) |
| Interrupção | 17% | 15% |

Linha do tempo V44 (`tools/linha_tempo.py`, média de 19 quadros da luta): quadro
33,35 ms; espera ociosa 9,1 ms; passo 1 17,6 ms; passo 2 6,5 ms; callback do
timer 2 0,32 ms + 3,4 ms nas voltas seguintes (2 por quadro), das quais a lista
2,8 ms; VBlank 1,0 ms. Nos trechos de 1 passo por quadro: passo 1 17,8 ms,
callback 3,7 ms, VBlank 1,0 ms = 22,5 ms, mais que 16,7 ms (por isso esses
trechos andam a metade da velocidade).

Conclusão: `PS2_T2_FIEL=1` fica. Tirar a lista de dentro da callback não acelera
sozinho: a lista continua custando ~2,8 ms de EE. Para 60/s (e para os trechos de
1 passo) cada passo com desenho precisa caber em 16,7 ms de EE, e hoje são
~21,7 ms (0,3 + 2,8 + 17,6 + 1,0): faltam ~5 ms.

## V45 — RTPS/RTPT mais rápidos

Perfil do GTE por comando (linha nova `gte:` no ELF com log; PCSX2, luta): RTPT
~520 por VBlank e ~2/3 do tempo do GTE; depois NCCT, MVMVA, NCLIP e GPF. No
console o RTPT custava ~612 ciclos (V44, `bench:`), ~2,2 ms por quadro.

`rtp_rapido()` em `gte.c`: mesmo resultado bit a bit, mas lê matriz/TR/constantes
uma vez, guarda FLAG num registro, grava MAC/IR e as filas SZ/SXY uma vez só e
conta os zeros da divisão com uma instrução (PLZCW). No PCSX2 o RTPT caiu de ~826
para ~615 ciclos no jogo (−25%). Conferido pelo autoteste do boot (rápido x
completo, que no console também testa o PLZCW) e por `tests/gte_test.c`
(800 mil estados aleatórios e de borda; 5 mutações de teste foram pegas).
Também saiu um comportamento indefinido antigo (`<< 12` em número negativo no
caminho rápido, achado pelo UBSan do `make test`), trocado por `* 4096`.

## V46 — leituras do jogo em linha (memória)

Perfil por função com as chamadas diretas (ELF de medida no PCSX2, luta): a nativa
`80036CAC` 17,7% e a conversão da lista (`8007E0B8`) 16,5%; o resto do jogo
espalhado em funções pequenas. Linha nova `nativas gte`: só 36% da `80036CAC` é
GTE; o resto é a rotina mexendo na memória. No objdump ela tem 9.652 instruções
(730 no original), com o teste de RAM, o do scratchpad e a marcação de código
sobrescrito em linha em cada acesso.

Dois achados no caminho de memória do código gerado (`runtime/fast_memory.h`):

1. Todo LW/LH do jogo chamava `memory.c`. O gerado usa `psx_cyc_load_word/half`
   (do `psx_cyc.h` do psxrecomp), que chamam `psx_read_word/half`. O
   `fast_memory.h` troca essas por `ps2_fast_read_*` com `#define`, mas o
   `psx_cyc.h` entra por `cpu_state.h` antes desses `#define`: o objdump mostrava
   `jal psx_read_word` com endereço fixo de RAM (ex.: `0x80095424` em `80029674`).
   O LB e as gravações já iam em linha. Agora `psx_cyc_load_word/half` viram
   `ps2_fast_cyc_load_word/half`. Variante compacta por padrão
   (`PS2_LOAD_COMPACTO=1`: só RAM KSEG0 em linha, scratchpad e o resto pela chamada
   de antes): mesmo tempo no PCSX2 que tudo em linha e 0,9 MB a menos de código.
2. O teste "é RAM?" cobria KUSEG, KSEG0 e KSEG1 (8 instruções, 2 desvios). O jogo
   só usa KSEG0 (contador `ram lenta` no `prof:`: 0 por VBlank com tudo em linha).
   `ps2_kseg0_ram()` resolve com 1 teste; KUSEG/KSEG1 continuam certos pela
   chamada a `memory.c`. Só isso tirou 404 KB do código.

| PCSX2, luta | V45 | V46 |
|---|---|---|
| quadros de 1 VBlank por janela (~2 s) | 54 de 91 | 118 de 119 |
| relógio do round | 96,6% | 99,1% |
| EE por passo com desenho (passo 1 + timer 2 + VBlank) | 17,0 ms | 15,1 ms |
| código (texto do ELF) | 9,70 MB | 10,30 MB |

Conferido: `tests/fast_memory_test.c` (caminho rápido x `memory.c` em RAM nos 3
segmentos e 4 espelhos, scratchpad e I/O; gravações conferem RAM, scratchpad e
`g_cod_sujo`/`g_cod_ver`: 244 mil casos, 0 diferenças, nos dois modos de
`PS2_LOAD_COMPACTO`) e `tests/fast_memory_gerado_test.c` (para a compilação se o
LW/LH do gerado voltar a não usar o caminho rápido). No console falta medir: o
PCSX2 não simula o cache de instruções (16 KB) e o código cresceu 0,6 MB.

## V46 no console (log de 20/11/2026)

Log em `ELF OU ISO TESTE/resultados_logs/tekken3_log_V46_console.txt` (comparação com a
V44 por `tools/pegar_log_console.sh`).

| Console, luta | V44 | V46 |
|---|---|---|
| EE por passo com desenho (passo 1 + timer 2 + VBlank) | 22,4 ms | **17,6 ms** |
| passo 1 | 17,6 ms | 13,4 ms |
| ciclos do EE por bloco do PS1 | 173 | 110 (−36%) |
| quadros por janela (~2 s) | 60 (59 de 2 VBlanks) | 66 (15 de 1 VBlank, 51 de 2) |
| relógio do round | 99,1–100% | 99,1% |
| RTPT especializado (bench) | 612 ciclos | 514 ciclos (V45) |

No console a V46 rendeu mais que no PCSX2 (−22% contra −11% no passo com desenho; o
console paga mais caro a chamada fora de linha que sumiu). Faltam ~0,9 ms por passo para
60 imagens/s: já aparecem quadros de 1 VBlank (15 por janela).

## V47 — música XA do CD (20/11/2026)

A música do Tekken 3 é XA-ADPCM no arquivo 1 do disco (8 canais intercalados, estéreo
37,8 kHz 4 bits, CD em 2x; o jogo escolhe o canal pelo Setfilter): ~18,75 setores por
segundo, 2.016 quadros por setor. Efeitos e vozes (SPU, libsnd) continuam mudos.

Caminho (`runtime/som.c`, kit 0.2.0 `kit/som.h`):

1. `cdrom.c` entrega cada setor de áudio lido; com o filtro certo ele entra numa fila de
   4 setores (cópia de 2.352 bytes).
2. Decodificação (fórmula do psx-spx, igual bit a bit à 1ª versão: `tests/som_test.c`)
   e conversão para 48 kHz por interpolação linear, com correção de até ±1% pelo nível do
   buffer do EE (alvo 6.144 quadros). Feita **1 grupo (1/18 de setor) por vez enquanto o
   jogo está parado no laço da espera do quadro** (`bios_hle.c`, PCs 80029720/80029728/
   8004CE54): cada conferência de IRQ nesse laço decodifica um grupo (~45 µs no PCSX2) e
   pede a próxima conferência em 4 (no laço, 256 levavam ~0,8 ms). Reserva no VBlank
   (6 grupos) só quando a espera não dá conta (2+ setores na fila ou buffer < 1/4).
3. Uma vez por VBlank, envio ao audsrv **sem esperar o IOP** (cliente RPC próprio,
   `SIF_RPC_M_NOWAIT`): o que o IOP aceitou do envio anterior sai do buffer; até 1.024
   quadros por envio. Sem música, silêncio do mesmo jeito (a fila do IOP nunca seca: o
   audsrv repete o que sobrou se ninguém manda nada). Fila do IOP: 20.480 bytes (~107 ms).
4. Troca de canal, Mute/Demute e Init do CD cortam na hora o som antigo do buffer do EE
   (no PS1 valem na hora), menos o envio em voo, que o VBlank seguinte resolve; o que já
   está no IOP (até ~107 ms) ainda toca. Sem música, a fila do IOP fica cheia de
   silêncio (até ~107 ms antes da música começar), para não secar.

Custo no PCSX2 (luta automática): decodificar ~0,81 ms por setor (~0,25 ms por quadro em
média); VBlank do som ~19 µs com música, ~11 µs sem. Captura do SPU2 em WAV: música
contínua na luta, 0 faltas; os buracos que aparecem (15 s no fim do round, ~100 ms
2 s depois da música recomeçar) existem igual na 1ª versão síncrona.

| PCSX2, luta (mediana das janelas) | V46 | V47 |
|---|---|---|
| relógio do round | 99,1% | 98,3–99,1% |
| quadros de 1 VBlank por janela | 118 | 110–116 |
| EE por passo com desenho | 15,1 ms | 13,4–14,8 ms (amostra da linha do tempo, P12) |

O que foi tentado e piorou (PCSX2), para não repetir:

| Onde decodificar | Resultado |
|---|---|
| Na entrega do setor (1ª versão, com 2 RPCs síncronos por VBlank) | 1 VBlank 118 → 74; o setor (1,4 ms) cai em qualquer ponto do quadro, até na lógica |
| Setor inteiro na entrada da espera, se faltasse > 2 ms para o VBlank | Quase nunca dava: a espera começa ~0,5 ms antes do VBlank; o tempo livre é depois dele |
| Idem, com a fase da última saída da espera | Relógio 93%: a saída vem ~4 ms depois da liberação do passo (callback do timer 2 com a lista) |
| 1/3 de setor no VBlank | Atrasa a IRQ do VBlank e a do timer 2 que libera o passo: 1 VBlank 110 |
| Também no laço de 80029A28 (chamado pela espera) | Relógio 93%: esse laço faz trabalho do jogo até o contador do timer 2 chegar no limite |

Falta o console: ouvir (volume, picotes) e o log (`som saida:` faltas/cortados,
`custo: decod ... maior pedaço`, `grupos na espera / no vblank`).

## V48 — vídeo da abertura (20/11/2026)

Sintoma (console e PCSX2): a abertura começava, travava no 1º ou 2º quadro e, ~10 s
depois, o jogo pulava para a tela de título/seleção. O catálogo (A4) achava que era o
tocador interpretado "não acompanhando"; não era.

Como foi achado: `make TRACE_FMV=1` (`runtime/trace_fmv.c`) escreve no log os comandos e
DMAs do MDEC, o DICR, cada setor do CD com o estado do anel do streaming do libcd e onde
o código está parado. O tocador (overlay carregado em 800B8D58 do setor 251748) espera
o próximo quadro em 800E1D34 (StGetNext → controle → VSync(-1)) e **desiste depois de
600 VBlanks sem quadro** — é o "pula para a seleção".

### Causa: DICR escrito em 8 bits

O libcd liga a IRQ do DMA3 só para o último setor de cada quadro, escrevendo **o byte 2
do DICR** (`sb` em 1F8010F6, função 80085710). O port só tratava o DICR em 32 bits:
o byte ia para um registrador qualquer, a IRQ do DMA3 ficava sempre ligada (pelas
escritas de 32 bits de outros códigos) e o callback de "quadro completo" (800858B8)
rodava a cada setor. O anel funcionava por acaso até a 1ª perda de setor: aí o libcd
volta ao início do quadro (`fa48`), que estava errado, e deixa posições marcadas como
ocupadas na frente da escrita → anel "cheio" para sempre, nenhum setor mais é lido.
Correção (`io.c`): leitura e escrita do DICR em 8/16 bits nos endereços F4–F7 (bytes
não escritos mantêm o valor; nos flags, que limpam com 1, vai 0).

### Velocidade do vídeo (PCSX2; o filme tem ~20 quadros/s e a música vem no mesmo fluxo)

| | quadros/s | setores de música por 2 s (PS1: 37,5) |
|---|---|---|
| só a correção do DICR | ~4 | ~25 (o CD esperava o jogo) |
| + VLC nativo, IDCT esparso | ~10 | |
| + fim do DMA1 depois, imagem em bloco | ~14 | |
| + MDEC sob demanda, CD recupera atraso | **~20** | **37–38** (sem faltas no som) |

1. **VLC do overlay em C nativo** (`tools/nativas_overlay.txt`, `tools/nativo.py`): o
   decodificador de bits do quadro (800E2714, formato BS v3, feito à mão em assembly)
   rodava no interpretador (~70 a 110 mil instruções por quadro). O `nativo.py` agora lê
   rotinas de overlay do `.bin` e o `interp.c` usa a versão nativa só se a RAM tiver
   exatamente aquelas instruções. Conferido com `NAT_VERIFICAR=1` (25 quadros iguais ao
   interpretado, RAM e registros). O interpretador também guarda os alvos que não são
   função compilada (antes, busca binária em 22 mil funções a cada desvio).
2. **MDEC** (`mdec.c`): IDCT só com as colunas/coeficientes não nulos (os blocos do
   vídeo têm ~2,6 colunas e poucos coeficientes; ~200 multiplicações em vez de 1024),
   laços de 8 escritos à mão (o gcc do EE guardava as somas na pilha a cada
   multiplicação), cor calculada uma vez por amostra de croma. Saída igual bit a bit:
   `tests/mdec_test.c` compara com o decodificador antigo em 3.000 fluxos aleatórios.
   PC: 0,85 → 0,46 ms por quadro; PCSX2: ~70 → ~24 ms.
3. **Fim do DMA1 avisado depois** (`io.c`, 2 ciclos por palavra): com o aviso na hora,
   as 16 faixas do quadro rodavam dentro de UMA IRQ (o tratador do libetc vê o flag de
   novo e repete), ~70 ms sem atender o CD.
4. **Imagem para a VRAM em bloco** (`gpu.c`): o LoadImage de cada faixa passava pixel a
   pixel com duas divisões por pixel. `tests/gpu_test.c` compara com o caminho antigo
   (400 casos, máscaras, volta em x=1023, fim no meio da palavra).
5. **MDEC sob demanda**: o comando só guarda a entrada; os macroblocos são feitos
   quando o DMA1 pede cada faixa, como no PS1 (antes o quadro inteiro era decodificado
   dentro da escrita do CHCR do DMA0, sem atender interrupções). Comando novo ou tabela
   nova termina antes o que falta (teste "comando novo no meio").
6. **CD recupera o atraso** (`cdrom.c`): quando o drive esperou o jogo, o próximo setor
   sai assim que o jogo pede (até 2x mais rápido, até 8 setores de atraso). Antes cada
   demora se perdia e o vídeo + música andavam a ~92% nos trechos pesados.
7. **VSync sem desistir cedo** (`tools/transformar_gerado.py`, 80085B20): o `v_wait` da
   libetc desiste depois de `tempo<<15` voltas; compilado, isso dava ~2,5 ms e o VSync(0)
   do tocador escrevia "VSync: timeout" ~5 vezes por segundo. Limite 8x maior: 0
   timeouts no log inteiro (antes 14 no boot e 117 com a abertura).
8. Cópia da tela em 24 bits sem máscara por byte quando a linha não dá a volta na VRAM
   (5,8 → 4,0 ms a cada 3 VBlanks no PCSX2).

Revisão (2 rodadas, Antigravity + Codex): o fim do DMA1 saiu de `ps2_timers_poll` para
`ps2_dma_poll` (conferências de IRQ e escrita do modo de um timer) e, quando o MDEC entrega
depois, espera o tempo das palavras que faltavam; o CD não lê dois setores com menos de
meio período entre eles (um XA na recuperação de atraso deixava o seguinte sair na mesma
conferência); `ps2_cod_marcar_faixa` marca os grânulos de código contados a partir de
`g_cod_ini` (inclusive o último parcial); a estrutura das rotinas de overlay ficou em
`runtime/nativo_overlay.h`; `nativo.py` aceita só o executável (tabela vazia). Concluir o
DMA1 também na leitura do DICR/I_STAT/CHCR foi testado e descartado (ver P17 no catálogo).

A abertura inteira toca até o fim (~96 s, tempo real) e vai para o título; captura do
som em WAV sem buracos durante o filme. Na luta (`medir_pcsx2.sh`) nada disso entra no
caminho: as diferenças contra a V47 vêm de outra luta (a abertura agora toca até o
START automático, a escolha de personagens muda: música do canal 1/4 em vez de 1/3); os
custos por unidade (GTE por comando, ciclos por polígono, ciclos por bloco) são iguais.

No console: medir com o `tekken3_log.elf` (linha `prof: ... | mdec X% (N quadros)` = quadros
decodificados em ~2 s; `som: xa N setores` ≈ 37 = CD em tempo real).

## V49 — exibição no console (21/11/2026)

Primeiro teste da V48 no console (vídeos e foto do usuário): a abertura toca inteira, mas
com três defeitos que o PCSX2 não mostra.

1. **Faixas pretas finas no vídeo**, em alturas que mudam de quadro para quadro. O caminho
   de 24 bits (`video_gs.c`, a cada 3 VBlanks) fazia `gsKit_clear` → envio da textura →
   sprite, num buffer só (sem troca). Enquanto a textura subia, a tela estava preta, e a TV
   mostrava isso na altura em que o feixe estava. Agora não limpa (o sprite cobre a tela).
2. **Lixo verde na tela inteira antes do "NAMCO PRESENTS" e uma linha no pé dela.** A
   cópia da VRAM do PS1 dentro do GS nunca era zerada: no console a memória do GS vem com
   o que o OPL deixou; no PCSX2 vem zerada. A área que o jogo ainda não escreveu aparecia.
   `gsr_init` agora preenche os 1024x512 com preto (a sombra em RAM começa zerada).
3. **Texto da tela do PUSH START com a parte de baixo "puxada"** (368x480 entrelaçado). No
   PCSX2 o texto sai certo. Mudança feita às cegas: `gsr_exibir` e o sprite de 24 bits
   amostram no centro de cada pixel da tela (meio texel), e `gsr_exibir` prende a leitura
   na área exibida (REGION_CLAMP, só no eixo que cabe na VRAM). No GS de verdade a amostra
   exatamente na borda de um texel pode cair no vizinho. A confirmar no console.

## V50 — texto do PUSH START e faixas claras no vídeo de demonstração (21/11/2026)

Teste da V49 no console (fotos e vídeo do usuário, HDMI e AV iguais): o texto da tela do
PUSH START continuava com a parte de baixo "puxada", e no vídeo de demonstração (320x240,
depois do título) apareciam faixas claras piscando em cima e embaixo.

1. **Texto do PUSH START.** A tela é só de sprites texturizados (~83 por quadro, nenhum
   polígono; contado no PCSX2). Testes no GS de verdade (PCSX2, PR 14029): num sprite de
   largura/altura que não é potência de 2, o U/V interpolado em cima de um inteiro arredonda
   **para baixo**, menos na 1ª coluna/linha; o PCSX2 arredonda para cima, por isso lá saía
   certo. Com UV inteiro (`u<<4`), cada letra saía deslocada 1 texel no console (1ª coluna e
   1ª linha repetidas). `gsr_sprite` agora soma meio texel (+8) ao UV: o sprite do PS1 é
   sempre 1:1, então o centro do texel escolhe o mesmo texel nos dois. Os polígonos ficaram
   como estavam (no PS1 a amostra trunca; meio texel mudaria a luta).
2. **Faixas claras no vídeo.** O vídeo de 24 bits é copiado da sombra da VRAM em RAM, mas
   com o GS os preenchimentos e desenhos só vão para o GS (V19 2.1): nas linhas fora do quadro
   do vídeo a sombra tinha sobras de telas antigas. Novo mapa `ps2_gpu_sombra_ok` (1 bit por
   halfword) diz onde a sombra está em dia: LoadImage e preenchimento que escreve na sombra
   marcam em dia, o 1º desenho só no GS depois de uma escrita marca a área de desenho como
   velha, a cópia VRAM->VRAM leva o estado da origem. Quando a tela passa a 24 bits, o que a
   CPU não escreveu no último meio segundo também fica velho (`ps2_gpu_sombra_envelhecer`; num
   PS1 essas linhas ficariam fora da área visível da TV de tubo). Velho sai preto.

Revisão (2 rodadas): a 1ª versão zerava o mapa no VBlank da troca para 24 bits (apagava o
1º quadro já enviado e deixaria preta uma imagem estática de 24 bits); virou o mapa "em dia"
com envelhecimento. Também: 1 bit por halfword (antes grupo de 64), pixel de 24 bits confere
o 1º e o 3º byte, cópia bloqueada pela máscara ou sobreposta fica velha, preenchimento que dá
a volta na VRAM em pedaços também no GS. PCSX2: abertura ~19–20 quadros/s, título certo,
luta normal.

## V51 — erros do gs_render.c achados no port do SOTN (21/11/2026)

O port do Castlevania SOTN copiou o `gs_render.c` daqui, e a revisão de lá achou 3 erros
que valem também para o Tekken. Conferidos no código e corrigidos:

- `gsr_copiar`: os bits de `TRXPOS.DIR` estavam trocados (bit 0 = de baixo para cima,
  bit 1 = da direita, GS User's Manual). Cópia de VRAM com sobreposição saía errada no GS.
- Versão de página de 8 bits em 32 bits (`a + b<<16`): repetia depois de 65536 envios numa
  coluna, e a textura podia sair velha. Agora em 64 bits.
- `XYZ2` sem limite: sprite + largura passando de 4095 vazava o X para o Y. Sprites,
  retângulos e linhas agora presos em 0..4095. Polígonos não precisam (vértice de 11 bits +
  deslocamento de 11 bits = -2048..2046) e ficam sem o limite, que custava ~57 ciclos por
  polígono no bench (519 → 576; sem ele, 539).
- Também de lá: `gsr_linha` marca desenho (TEXFLUSH antes de ler textura de 15 bits) e
  `gsr_exibir` reafirma `PRMODECONT`.

Revisão (Codex/Antigravity), sem ALTA. Corrigidos: a cópia no GS agora divide nas bordas
de 1024x512 (a sombra dá a volta); a sombra copia na mesma ordem do GS (com sobreposição os
dois batem; as texturas de 4/8 bits saem da sombra); `xyz_vert` sem deslocamento de número
negativo. Descartados: PRMODECONT depois do gsKit (o `base()` já restaura), centralizar
`desenhou_desde_flush` e tirar "Vxx" dos comentários (estilo).

PCSX2: intro igual, luta normal (bench de boot 539 ciclos por polígono, V50 519).
**Falta o console** (junto com o teste da V50).

## V52 — kit 0.2.1: achados do port do SOTN no kit compartilhado (21/11/2026)

O port do SOTN copiou o `kit/` daqui e a revisão de lá listou pontos que não corrigiu
(catálogo do SOTN, etapa 4). Conferidos no Tekken e corrigidos (detalhes no
`kit/CHANGELOG.md`, 0.2.1):

- log: o aviso de bytes descartados ficava preso (só há perda com o buffer cheio, e aí ele
  não cabia); agora vai direto para o arquivo. Escrita parcial continua; `errno` nos erros.
- pendrive: `host:` reconhecido pelo `:`; reset/sincronização do IOP com prazo (antes
  laços sem fim).
- controle: chamar `kit_controle_iniciar` de novo não recarrega o PADMAN.
- som: ligação ao RPC do audsrv com prazo, sem esperar dentro da chamada.
- audsrv (IOP): bloco do reamostrador que cruzava o fim da fila (taxas abaixo de 48 kHz);
  falhas de thread/memória/semáforo propagadas; faixas do CDDA (a faixa 1 não tocava).
  **A 48 kHz (o que o Tekken usa) o caminho do som não muda.**

Descartados: `st.ok` do libpad (como no SOTN: a leitura só acontece com o controle
estável); buffer de 256 KB do log sempre na RAM (0,8% dos 32 MB, sem efeito); `audsrv_quit`
no prazo do som (também espera o IOP e travaria no mesmo caso); ELF lançado de `mc0:`/
`hdd0:` e avanço fracionário do reamostrador a 11025 Hz (não usados; anotados no CHANGELOG).
Revisão em 2 rodadas (as 2 ALTAS eram casos de borda das próprias correções: prazo do bind
que não pegava chamada presa, reenvio do bind com pedido pendente, aviso pela metade).

PCSX2: música XA a 48 kHz sem faltas, luta normal; caminho do pendrive (`FORCAR_IOP=1`):
IOP reinicia, log gravado, som e controle ok. **Falta o console** (junto com V50/V51).

### Auditoria V4 do Qwen (17/11, logs V36/V37) conferida na V52
Quase tudo já feito ou superado: 3a log travando (log a cada ~30 s só com o CD parado e
ELF de jogo sem log, desde a V38); 3b (medida sem log da V42: 41,06 s); 3c "quebra de
pixel" no início (lixo do GS não zerado, V49); 3d listas cíclicas (guarda
`g_ps2_listas_ciclicas` mantida); 4.2 hash da OT (V40, redesenho reconhecido sem reler os
comandos); 4.6 `gsr_poligono_pk` (V29). Exagerado: 4.1 telemetria (mfc0 custa ~1 ciclo; a
busca em `quem_t` é por 10 nativas). Ainda valem: C nativo de 8003A87C/8003B1C4/80028BCC
(4.3), dados quentes na scratchpad do EE (4.5), GTE no VU0 (4.7).

## V53 — efeitos e vozes: SPU do PS1 tocada na SPU2 (21/11/2026)

Console (V51/V52, 21/11): texto do PUSH START certo, faixas do vídeo de demonstração
sumiram, placar ok, log gravado no pendrive (relógio do round 100%, 0 VBlank perdido).

Efeitos e vozes estavam mudos desde a V36 (sequenciador 80088BA8 pulado). Medido com ele
religado: ~950 escritas na SPU por 2 s (~8 por quadro) e poucos KON; custo no EE dentro da
variação. Caminho escolhido: a SPU2 do PS2 toca o mesmo ADPCM e o mesmo ADSR da SPU do PS1;
o EE só repassa.

- `runtime/spu.c`: cópia da memória da SPU (512 KB) no EE; escritas nos registradores e
  amostras (DMA 4, porta 0x1F801DA8) viram um pacote por VBlank, na ordem do jogo, mandado
  sem esperar; ENDX/ENVX vêm da resposta do pacote anterior (KON depois do envio conta como
  não terminado). Um pacote "nada" nos quadros sem comandos. IOP sem resposta por 1 s:
  desliga o canal (ENDX volta a "todas terminaram").
- kit 0.3.0: `kit_spu1_*` (canal RPC próprio sem esperar) e `iop/audsrv/src/ps1spu.c`
  (núcleo 0 da SPU2, memória do PS1 em 0x80000, afinação × 44100/48000, KON/KOFF juntados
  no pacote e gravados no fim com uma amostra de espera entre gravações).
- Dois tropeços no PCSX2: (1) com um servidor RPC só, a resposta do PCM ia para o pacote da
  SPU e o EE caía em `_request_end` (TLB miss em 0x10): o SIF RPC aguenta uma chamada em
  andamento por servidor; (2) com os dois servidores na mesma fila do IOP a resposta do PCM
  se perdia (música muda, "ocupado" em todo VBlank). Servidor da SPU em thread e fila
  próprias.
- PCSX2: o som do título e do começo da luta aparece (comparação de WAV com a V52), música
  inteira, 120 pacotes por 2 s todos respondidos, custo da luta na variação. O Tekken não
  escreveu na reverberação nos testes (fica para depois se faltar no console).
- Revisão em 2 rodadas (ALTAS: KON/KOFF das duas metades e de vozes diferentes no mesmo
  pacote — `sceSdSetSwitch` grava os 24 bits e a SPU2 lê uma vez por amostra; ENDX parado
  sem pacote; ordem do KON num envio forçado; `volatile`). Conferindo o retorno das transferências
  (pedido da revisão) apareceu um 3º tropeço: `sceSdSetCoreAttr(SD_CORE_NOISE_CLK)` estragava o
  ATTR do núcleo 0 e toda amostra enviada depois do 1º SPUCNT era recusada; agora o clock do
  ruído vai direto no registrador e os sons do menu apareceram (catálogo P26).
  **Falta o console.**

## V53 — efeitos e vozes: SPU do PS1 tocada na SPU2 (21/11/2026)

Console (V51/V52, 21/11): texto do PUSH START certo, faixas do vídeo de demonstração
sumiram, placar ok, log gravado no pendrive (relógio do round 100%, 0 VBlank perdido).

Efeitos e vozes estavam mudos desde a V36 (sequenciador 80088BA8 pulado). Medido com ele
religado: ~950 escritas na SPU por 2 s (~8 por quadro) e poucos KON; custo no EE dentro da
variação. Caminho escolhido: a SPU2 do PS2 toca o mesmo ADPCM e o mesmo ADSR da SPU do PS1;
o EE só repassa.

- `runtime/spu.c`: cópia da memória da SPU (512 KB) no EE; escritas nos registradores e
  amostras (DMA 4, porta 0x1F801DA8) viram um pacote por VBlank, na ordem do jogo, mandado
  sem esperar; ENDX/ENVX vêm da resposta do pacote anterior (KON depois do envio conta como
  não terminado). Um pacote "nada" nos quadros sem comandos. IOP sem resposta por 1 s:
  desliga o canal (ENDX volta a "todas terminaram").
- kit 0.3.0: `kit_spu1_*` (canal RPC próprio sem esperar) e `iop/audsrv/src/ps1spu.c`
  (núcleo 0 da SPU2, memória do PS1 em 0x80000, afinação × 44100/48000, KON/KOFF juntados
  no pacote e gravados no fim com uma amostra de espera entre gravações).
- Dois tropeços no PCSX2: (1) com um servidor RPC só, a resposta do PCM ia para o pacote da
  SPU e o EE caía em `_request_end` (TLB miss em 0x10): o SIF RPC aguenta uma chamada em
  andamento por servidor; (2) com os dois servidores na mesma fila do IOP a resposta do PCM
  se perdia (música muda, "ocupado" em todo VBlank). Servidor da SPU em thread e fila
  próprias.
- PCSX2: o som do título e do começo da luta aparece (comparação de WAV com a V52), música
  inteira, 120 pacotes por 2 s todos respondidos, custo da luta na variação. O Tekken não
  escreveu na reverberação nos testes (fica para depois se faltar no console).
- Revisão em 2 rodadas (ALTAS: KON/KOFF das duas metades e de vozes diferentes no mesmo
  pacote — `sceSdSetSwitch` grava os 24 bits e a SPU2 lê uma vez por amostra; ENDX parado
  sem pacote; ordem do KON num envio forçado; `volatile`). Conferindo o retorno das transferências
  (pedido da revisão) apareceu um 3º tropeço: `sceSdSetCoreAttr(SD_CORE_NOISE_CLK)` estragava o
  ATTR do núcleo 0 e toda amostra enviada depois do 1º SPUCNT era recusada; agora o clock do
  ruído vai direto no registrador e os sons do menu apareceram (catálogo P26).
  **Falta o console.**

### V53 no console: tela preta (21/11)
O log parava em "som: iniciando". Com passos gravados na hora e ELFs de diagnóstico (jogo da
V53 com o kit da V51, da V52 e o atual sem a thread nova): V51 e V52 entram, o atual trava em
"carregar audsrv". Causa: `static u16 ... spucnt = 0xC000` em ps1spu.c deixou a .data com
0x142 bytes e o FileSiz do IRX em 0x4472; o IOP zera a .bss a partir dali com escritas de 4
bytes e travou (o PCSX2 aceita). Corrigido (spucnt começa em iniciar()) e o kit.mk recusa IRX
com FileSiz não múltiplo de 4 (catálogo P27). Ficam: etapas e passos da inicialização no log,
log com a versão no nome, reserva do disco em mass:APPS/TEKKEN3/.

### V54: efeitos no console — atraso, ruído e travadas (21/11)
No console a V53 entrou e tocou os efeitos, mas com atraso, tom estranho, cortes e chiado.
O que mudou:
- KON na hora: quando o jogo liga uma voz (metade alta do KON, ou o KON de 32 bits), o
  pacote vai para o IOP já, sem esperar o VBlank (até ~17 ms a menos de atraso). Só manda se
  alguma voz foi ligada desde o último envio (o driver grava KON=0 todo quadro).
- Memória do PS1 na SPU2 zerada no início (512 KB, antes do volume principal): no console
  ela vem com o que o OPL deixou, e o PCSX2 zera.
- `gravou_chave` fica entre pacotes: dois pacotes seguidos podem chegar em menos de uma
  amostra.
- Opções no make: `SPU2=0`, `SOM_SEQ=0`, `LOG_MARCA=`. ELF "sem efeitos"
  (`APPS/T3SEMEFX`, `SOM_SEQ=0 SPU2=0`) para comparar no console.
- Revisão (1 rodada, sem ALTA): (a) resposta do pacote anterior lida antes de cada envio
  (um envio no KON logo depois da resposta chegar a sobrescrevia: ENDX/ENVX parados);
  (b) "envios no KON" conta só envios aceitos; escrita de 1 byte não dispara envio;
  (c) último bloco da limpeza limitado ao fim, e bloco recusado conta como erro da SPU2 no
  log. Descartado: zerar `gravou_chave` por pacote (custa só 25 µs por pacote com chave);
  adiar os comandos até a limpeza terminar (os dados do jogo no pacote não seriam mandados
  de novo).
- PCSX2: respostas = pacotes, 0 ocupado, 0 perdidas; custo da luta dentro da variação (duas
  medidas da mesma V54 já variam ±20%). **Falta o console.**
- Ficam: ENVX um quadro atrasado (pode cortar), ADSR ~9% mais rápido na SPU2 a 48 kHz,
  reverberação, CDDA (faixas 2 e 3).

### V55: apresentação do round na velocidade certa (21/11)
Medido no console com a placa de captura (UGREEN 15390, /dev/video0 + ALSA U15390; ligar
"Digital In Capture Switch" com amixer antes de gravar) e o PS2 pela rede (ps2link):
- A SPU2 responde no console igual ao PCSX2 (make SPU_TRACE=1: KON/KOFF e ENDX/ENVX por
  VBlank em host:spu_trace.bin, tools/comparar_spu_trace.py). As falas "Round / One /
  Fight" saíam tarde porque a apresentação do round andava na metade da velocidade.
- Referência: Tekken 3 original no mednafen (Xvfb + -qtrecord): cenário -> ROUND 1 1,0 s,
  -> READY? 0,8 s, -> luta 0,8 s. Console V54 (pendrive): 2,2 / 2,4 / 2,2 s.
- Causa: o laço 800508C4 só roda o passo de recuperação sem desenho quando 8002AE58
  devolve 1 (luta). Na apresentação devolve 0 (2/4/6 no fim do round); no PS1 ela cabia
  em 1 VBlank, no PS2 cada imagem leva 2.
- Correção (RECUP_APRES=1, padrão): ps2_luta_sem_recup() libera a recuperação também no 0
  e troca s0 por 1, porque o jogo grava s0 na marca 800ADCA4 ("passo sem desenho"); com 0
  a marca não ligava e o jogo caía para o navegador (1ª tentativa).
- Console (rede): 1,8 / ~1,0 / ~0,6 s.
- Pela rede o jogo anda mais devagar que pelo pendrive (CD lido pelo ps2link; relógio 86% x
  99%): medir velocidade sempre pelo pendrive. Sem log, o stdout fica calado também pela
  rede (antes cada printf esperava o PC). make LOG_REDE=1 grava o log em host:.
- Ferramentas: SEM_MUSICA=1 (só efeitos), SPU_TRACE=1, LOG_REDE=1, RECUP_APRES.
- Revisão em 2 rodadas: log da rede saiu do git; log em host: e aviso de falha; troca do
  teste em todas as cópias (o build para se faltar); flags normalizadas.
- Fica: a luta a ~30 imagens/s (passo com desenho ~16 ms no console, meta 16,7 ms em 1
  VBlank) — próximo alvo.

### Por que não chega a 60 imagens/s — medição com os contadores do R5900 (21/11)
**Conta do quadro** (log da V53 pelo pendrive, 20 quadros de luta com 2 passos): passo 1
(lógica + montagem do desenho) 14,13 ms; lista para o GS 3,42 ms; resto do tratador do
timer 2 0,73 ms; tratador do VBlank 0,62 ms; música ~0,2 ms. Soma 19,1 ms para caber em
16,68 ms: sobram ~2,5 ms (13%), com folga ~3,5 ms. Por isso cada imagem leva 2 VBlanks
(passo 2 de recuperação 5,06 ms e ~8 ms ociosos).

**Contadores de desempenho** (`make PMC=1`, runtime/pmc.h; só no console — o PCSX2 não
conta cache). Por janela de ~2,2 s (650 M ciclos), V55 pela rede, luta:

| região | ciclos | instruções | faltas cache instr. | faltas cache dados |
|---|---|---|---|---|
| jogo (código recompilado) | 265 M | 226 M | 2,3 M | 0,80 M |
| nativas (C de tools/nativo.py) | 134 M | 85 M | 1,32 M | 0,31 M |
| lista (conversão para o GS) | 63 M | 61 M | 0,03 M | 0,77 M |
| irq (VBlank + timer 2, fora a lista) | 65 M | 40–75 M | 0,44 M | 0,33 M |
| espera (ociosa) | 115 M | 120 M | 0,08 M | 0,14 M |

Ajuste entre as regiões: falta do cache de instruções ~30–45 ciclos, de dados ~13–23,
CPI base ~0,65–0,77. Ou seja: ~4,2 M faltas de instrução por janela = ~145 M ciclos,
perto de 1/4 de todo o tempo do EE (mais ~40 M das de dados). TLB: 0 faltas.

**Causa**: tamanho do código. O cache de instruções do EE tem 16 KB.
- `ps2_nat_80036CAC` (a rotina mais pesada, ~11,5% do tempo) tem **42.716 bytes** para
  729 instruções do PS1 (2.916 bytes): 10.686 instruções, 1.758 lw, 1.358 sw, 1.084 lui,
  618 nop, 229 jal (109 psx_read_word, 85 psx_write_word no caminho lento). Cada acesso à
  memória leva dois testes (RAM KSEG0, depois scratchpad — que é onde ela trabalha) e a
  chamada lenta em linha. As nativas rodam a 1,58 ciclo por instrução (a lista: 1,03).
- Código gerado: 20.633 funções, 7,89 MB, para 1,16 MB do executável do PS1 (~6,8x).
- Tratador do VBlank: 0,62 ms por VBlank na libpad (80092D34), que espera tempo real pelo
  contador do timer 2 (800951B8/800951D8) e pela 2ª porta sem controle.

**Plano** (por ganho esperado):
1. Nativas compactas: RAM e scratchpad numa janela só (scratchpad 8 MB depois da RAM,
   endereço & 0xFFFFFF), acesso sem teste e alinhado (lw/sw); conferir com NAT_VERIFICAR.
2. Esperas da libpad no VBlank.
3. Mesma janela no código gerado das funções do jogo (as de biblioteca que mexem em E/S
   continuam com o teste).
4. Lista: `pref` nos nós; RTPT com MMI.
Medir cada passo no console com PMC=1 (faltas e ciclos por região).

`8004A364` nativa (commit 61d6cc9) não mudou o tempo; `80028BCC` é o laço principal
(o perfil sem PS2_PROF_DIRETO somava nela o que ela chama).

## V56 — rumo a 60 imagens/s: código menor, controle sem espera, cache de dados fixo (21/11)

Medido no console pela rede (`make AUTO_START=1 LOG_BLOCOS=15 LOG_REDE=1 PMC=1`; resumo com
`tools/pmc_resumo.py -r LOG`, linha do tempo com `tools/resumo_log.py LOG`). Pela rede o CD
atrasa o jogo: velocidade de verdade só pelo pendrive (ainda não feito).

**1. Rotinas nativas compactas** (`runtime/janela.h`, `tools/nativo.py`). RAM e scratchpad do
PS1 numa "janela" só (`g_ps2_janela`: RAM em +0, scratchpad em +0x800000), acesso por
`endereço & 0xFFFFFF`, sem teste e alinhado. `g_ps1_ram` e `g_ps2_scratchpad` são macros para
dentro dela (um objeto só: ver a revisão abaixo). `ps2_nat_80036CAC` 42.716 → 8.136 bytes; as
12 nativas 120 KB → 24 KB. Conferido com `NAT_VERIFICAR=1` no PCSX2: 250 comparações, 0
diferenças, 0 acessos fora da janela. Console: faltas de cache de instrução nas nativas 1,32 M
→ ~0,55 M por janela; 23 → 5–9 por polígono.

**2. Código gerado**: leituras e escritas relativas ao `sp` pela janela (`ps2_pil_*`,
`tools/transformar_gerado.py`) e a marcação de código sujo com um teste só (`g_cod_pag`: 1
byte por página de 4 KB que tem código compilado; `ps2_cod_marcar_lento` em `codigo.c`).
`.text` −1,19 MB. Conferido com `GEN_OPT="-O2 -DPS2_JANELA_VERIFICAR=1"`: 0 acessos fora.

**3. Esperas da libpad no VBlank** (`io.c: ps2_timer_pad_adianta`, `pad.c:
ps2_pad_ack_ocupado`). O tratador 80092D34 espera tempo real pelo contador do timer 2
(800951B8 marca, 800951D8 compara, laços em 800932A0/80093478). Enquanto ele roda, cada leitura
do contador anda 1024 unidades, exceto com ACK a caminho ou recém-entregue. Duas versões
passaram no PCSX2 e **falharam no console** (0 leituras completas do controle):
- 1ª: a leitura que entregava o ACK também saltava, e o laço testa o prazo **antes** de olhar
  o I_STAT;
- 2ª: a leitura do contador entregava o ACK (`ps2_pad_tick`) e logo depois o jogo limpa o
  I_STAT (800932A0 chama 800951B8 e só então 800936AC): o ACK era apagado.
`tests/pad_espera_test.c` refaz as rotinas na ordem do jogo e roda 672 combinações de tempo:
falha com as duas versões erradas. Console: tratador do controle 0,56 → 0,40 ms, 11.420
leituras em 4 min.

**4. Lugar fixo no cache de dados** (`runtime/quente.h`, `ram.h`). Entre duas compilações com
o mesmo código da lista, as faltas de cache de dados foram de 7,7 para 15,1 por polígono: o
código encolheu, os dados mudaram de endereço e passaram a disputar os mesmos conjuntos (8 KB,
2 vias, conjunto = endereço mod 4 KB). Varredura da pilha (`make PMC=1 PMC_VARRE=1`,
`tools/varre_pilha.py`): só mudar a pilha da lista de 256 em 256 bytes deu de 6 a 18 faltas e
910 a 1.220 ciclos por polígono. Agora:
- janela alinhada em 4 KB; estado da CPU do PS1 e `g_cod_pag` num bloco quente alinhado;
- variáveis do caminho de cada polígono (`gpu.c`, `gs_render.c`) na seção `.bss.quente`, logo
  depois do bloco;
- pilha do EE em posição fixa (mod 4 KB) no laço de despacho, na lista e nas nativas.
Varredura depois: lista 6,6–6,8 faltas por polígono em 7 das 8 posições (2048: 11,9); nativas
~6. Escolhidas lista 0xC00, nativas 0xA00, jogo 0xE00.

**Resultado** (linha do tempo do console, quadros da luta; passo 1 = lógica + montagem do
desenho, sem as interrupções):

| | V53 (pendrive) | passo 1 (rede) | V56 sem o item 4 (rede) |
|---|---|---|---|
| passo 1 | 14,13 ms | 11,58 ms | 11,03 ms |
| tratador do controle | ~0,5 ms | 0,56 ms | 0,40 ms |
| lista de desenho | 3,42 ms | 3,42 ms | 3,88 ms (cache, item 4) |
| quadros em 1 VBlank | 0 | 26 de 73 | 39 de 82 |

O quadro cabe em 16,7 ms com ~0,5 ms de folga: alterna entre 60 e 30. Com o item 4 as faltas
da lista voltaram a ~6,7 por polígono (medido na varredura, cena de demonstração).

**Revisão** (Codex e Antigravity, 21/11): corrigidos o aliasing (RAM e scratchpad eram
apelidos do montador declarados como arrays distintos: o gcc podia reordenar uma escrita pela
janela e uma leitura por `g_ps1_ram` do mesmo lugar), `make test` que mascarava falha do teste
novo (`| tail`), `PMC_VARRE` sem `PMC`, limpeza do scratchpad de 4 bytes (`sizeof` de macro) e
o parser do PMC. Descartados: pilha fora da RAM (o jogo não faz; 0 acessos fora na
conferência) e escrita pela pilha sem marcar código (a pilha nunca está em página de código).

**Falta (22/11)**: rodada de 2 min pela rede com tudo junto (`~/cap_tmp/rodar_quando_pronto.sh`)
e o teste pelo pendrive com a placa de captura.

## V57 — som: o que falta na SPU2 (22/11)

Lista de pendências vinda de outra conversa (reverb, ADSR, ENVX, README), conferida
com medidas antes de mexer (linha `spu2:` do log ganhou contadores):

- **Reverberação: o Tekken 3 não usa.** PCSX2 (abertura, título, demonstração e luta) e
  log do console V53 (164 janelas): 0 escritas em 1DC0–1DFF, EON sempre 0, bit 7 do
  SPUCNT nunca liga, mBASE escrito 1 vez no início. Não foi feita (não haveria como
  ouvir nem conferir). Mapeamento conferido (spu2regs.h do ps2sdk, Reverb.cpp do PCSX2)
  documentado no `ps2/README.md`, "Som", para um port que use (SOTN).
- **ADSR ~9% mais rápido**: só documentado (degraus de taxa de ~15–25%).
- **ENVX**: o jogo lê o ENVX das 24 vozes a cada quadro (~2.850 leituras por 2 s); o
  ENDX nunca. O IOP lê o ENVX microssegundos depois de gravar o KON, quando o envelope
  ainda é 0, e o jogo via a voz nova com envelope 0 no quadro seguinte (no PS1 o ataque
  já teria subido). Em vez de um RPC síncrono por quadro (o jogo lê todo quadro: custaria
  EE todo quadro), `spu.c` devolve 0x7FFF para a voz ligada até chegar a resposta de um
  pacote posterior ao KON (no mesmo VBlank do KON, 0, como no PS1). PCSX2, luta
  automática: leituras de ENVX 0 em voz recém-ligada 130 (em 252 KONs) → 0 (em 257);
  relógio 99,2% nos dois. `PS2_ENVX_ATAQUE=0` volta ao anterior. Pode ser parte dos
  "cortes" dos efeitos vistos no console na V53 (o alocador de vozes da libsnd lê o ENVX):
  conferir no console.
- README: seção "Som (SPU → SPU2)" com a tabela de registradores, conversões e limitações.

### V57 no console pela rede, com tudo junto (22/11)

Rodada de confirmação (`make AUTO_START=1 LOG_BLOCOS=40 LOG_REDE=1 PMC=1`, 125 s,
`~/cap_tmp/log_v57.txt`), comparada com a da V56 sem o lugar fixo no cache
(`log_lay1.txt`), por `tools/pmc_resumo.py -r`, medianas de 16 janelas de luta:

| | V56 sem o item 4 | V57 |
|---|---|---|
| quadros por janela (em 1 VBlank) | 82 (39) | 98 (72) |
| jogo: ciclos por polígono | 2.989 | 2.543 |
| jogo: faltas de cache de instrução por polígono | 25,7 | 19,3 |
| lista: ciclos / faltas de dados por polígono | 1.100 / 15,1 | 941 / 5,0 |
| nativas: ciclos / faltas de dados por polígono | 1.306 / 6,0 | 1.189 / 5,6 |

Melhores janelas: 95 de 109, 91 de 108, 88 de 105 quadros a 60/s. Sem erros, controle
lido (3.898 leituras), 0 leituras de ENVX zerado em voz recém-ligada (116 KONs). Pela
rede o CD atrasa o jogo: falta o pendrive com a placa de captura.

### V57 no console pelo pendrive (22/11, OPL, placa de captura)

Gravação de 172 s (`~/cap_tmp/cap_v57.mkv`; quadros em
`ELF OU ISO TESTE/resultados_logs/captura/V57_pendrive_*.png`): abertura, título,
seleção, luta contra a Xiaoyu (vitória) e contra o Paul. Painel do jogo, amostrado a
cada 7 s:
- **luta contra o Paul: QUADROS/S 59 em todas as amostras**; contra a Xiaoyu 49–60
  (maioria 56–59); 11 e 24 nas telas de carregamento;
- **RODADA 98,5–100%**.
Antes (V54/V55 no console): ~28–30 imagens/s na luta.

A contagem de quadros novos na própria gravação dá 30 por segundo em tudo, até no
título parado: o adaptador entrega 480i como 30 quadros completos. Para medir imagens/s
no console vale o painel (QUADROS/S) ou o log, não a diferença entre quadros capturados.

## V58 — versão de jogar sem medição (`make LIMPO=1`, 22/11)

O `tekken3.elf` (`gerar_elf.sh jogo`) já não gravava log, mas ainda media: painel na tela,
relatório de 2 s montado e jogado fora, cronômetro e tabela de 90 KB por despacho de
função (~80 mil por segundo), cronômetro por polígono, por rotina nativa e por comando da
GPU, perfil por amostras na checagem de interrupção e a linha do tempo (`ps2_diag`).
`LIMPO=1` tira tudo isso; com log fica só a linha `limpo:` (relógio) e `quadros:`.

Console pela rede, mesmo roteiro automático, logo uma depois da outra
(`~/cap_tmp/log_medido.txt`, `log_limpo.txt`), janelas de luta:

| | com medição | LIMPO=1 |
|---|---|---|
| quadros por janela (mediana) | 96 | 100 |
| quadros em 1 VBlank | 759 de 1058 (71,7%) | 1286 de 1695 (75,9%) |

Os ~10 quadros de 3+ VBlanks por janela são iguais nas duas: são leituras do CD pela
rede, síncronas (`CD le` seguido de ~31 ms parado na linha do tempo); pelo pendrive a
leitura é assíncrona. Por isso a rede subestima o console.

`gerar_elf.sh jogo` passa a usar `LIMPO=1` (sem painel); o `tekken3_log.elf` continua
com painel e log para medir.

## V59 — controle 2 (22/11)

Achado no primeiro teste pela bancada automática (ps2bot, `~/PROJETOS/AUTOMACAO_PS2`):
o contador de leituras da porta 2 na placa ficava parado com o jogo aberto, e
`ps2ctl press 2:START` não fazia nada. Três pontos:

1. `main.c`: `kit_controle_iniciar(1)` — o 1 é o número de portas; só a porta 1 era
   aberta no PS2 (`padPortOpen`).
2. `video_gs.c`: só `kit_controle_ler(0, …)`.
3. `pad.c`: o SIO0 imitado só respondia `0xFF/ACK` ao `0x01` na porta 1; o jogo
   pergunta pelas duas portas e via a 2 vazia.

Correção: as duas portas abertas e lidas a cada VBlank; `ps2_pad_ligado[2]` (a 1 sempre,
como antes; a 2 só com controle 2 conectado, como num PS1 sem nada na porta 2). Da
revisão: trocar o bit de porta do `JOY_CTRL` no meio de uma conversa a encerra (o
aparelho da outra porta não estava selecionado). `pad_espera_test` cobre porta 2
desligada, ligada e a troca no meio.

Console pela rede (`bancada.elf`, LOG_BLOCOS=15 LOG_REDE=1): contador da porta 2 no
ps2bot subindo ~64/s (antes parado); VS MODE: cursor 2P move só com o controle 2,
vantagem de vida só do 2P, na luta `2:DOWN` agacha só o lutador da direita e `UP` do
controle 1 faz só o da esquerda pular.

Observação para os roteiros da bancada: no menu do Tekken o toque padrão do `ps2ctl`
(120 ms) e um de 50 ms andaram duas opções; 30 ms anda uma. O menu cai na demonstração
depois de poucos segundos parado — mandar a navegação num comando só.

### V59 — analógico, telas de início, memory card, CD-DA e desempenho (22/11)

Tudo testado no console pela bancada ps2bot (rede), com a placa de captura.

- **Analógico** (kit 0.3.3): DualShock posto em modo analógico sem trava; o stick
  esquerdo soma ao direcional (zona morta 64; o direcional manda no eixo apertado) nos
  dois controles. Console: stick 2 para baixo agacha o lutador 2, stick 1 move o 1.
- **Telas de início** (`runtime/abertura.c`): a vinheta da Retrogames, o título e
  "Apoie o projeto" com o QR do Pix, iguais às do Mario/Yu-Gi-Oh; fundo desenhado no
  código (nada da Namco). START pula; X/START ou 20 s seguem. `make ABERTURA=0` tira.
  Fundo em cache (redesenhar todo quadro perdia VBlanks). O relógio do EE (32 bits a
  294,9 MHz) dá a volta a cada ~14,5 s: tempo somado quadro a quadro.
- **Memory card**: o Tekken usa a API de arquivos da BIOS (`open("bu00:BASLUS-00402TEKKEN-3")`,
  `_card_load`, `write_card_sector(0x3F)`). `runtime/cartao_ps1.c` imita um cartão do PS1
  (imagem .mcr de 128 KB); `bios_hle.c` atende open/read/write/close/lseek/firstfile/
  nextfile/erase/format e `_card_*`. A imagem fica em `mc0:/TEKKEN3PS2/TEKKEN3.MCR`, com
  `icon.sys` e ícone 3D (`tools/gerar_icone.py`, desenho "T3 PS2"); gravada 1 s depois do
  jogo salvar, em etapas (uma por VBlank). O `rom0:MCSERV` não renomeia (`mcRename` = -1
  no console): grava `~TEKKEN3.MCR` inteiro, apaga e grava `TEKKEN3.MCR`, apaga o `~`.
  Leitura: `.MCR` bom; senão `~` (e regrava o `.MCR` direto); erro -> cartão desligado na
  partida (não grava por cima). Console: CARD SAVE "SAVE OK!", reiniciar, o jogo lê
  sozinho, CARD LOAD "LOAD OK!"; a recuperação pelo `~` também foi exercitada.
- **CD-DA**: a faixa 2 (2:37) é a única com som (a 3 é zeros). O jogo não mandou Play em
  nenhum teste; `make CDDA_TESTE=1` toca 10 s. Faixa aberta na partida; posição a 75
  setores/s (GetlocP, fim, mudo); leitura à frente até o nível do buffer (ler no ritmo
  exato deixou o buffer sem folga: correlação 0,24-0,44). Console: cada segundo da faixa
  achado na gravação 1,00 s depois do anterior, correlação 0,53-0,72 (controle 0,24).
- **Desempenho**: checagem de interrupção só nos desvios para trás (28.469 de 34.683
  tiradas, ~8 instruções cada; ELF 11,5 -> 10,4 MB). Luta no ARCADE pela rede, A/B duas
  vezes: quadros em 1 VBlank 78,0/78,9% -> 83,3/83,9%, mediana 97 -> 101 por janela.
  `make IRQ_FRENTE=1` volta ao de antes.
- Uma partida pela rede (bench_b2) travou antes do vídeo (log parado em "som: pronto");
  não se repetiu (b3). Fica a observar.
- Chão "girando": ainda sem referência do PS1 com a câmera girando.


## V60 — rumo a 60 imagens/s: o que restava da lista (23/11)

Itens que ficaram da medição de 21/11: rotinas 8003A87C e 8003B1C4 em C à mão, `pref` na
lista de desenho, RTPT com MMI. Medido antes de mexer, com o perfil por função exato
(`GEN_OPT="-O2 -DPS2_PROF_DIRETO=1"`, luta no ARCADE pela bancada, `~/cap_tmp/bench_luta.sh`).

**8003A87C e 8003B1C4 não valem a reescrita.** Os 17–19% e 5,7% de antes vinham do perfil
sem `PS2_PROF_DIRETO`, que soma numa função o que ela chama direto. 8003A87C só chama as
rotinas de cada lutador (83 instruções, nenhuma conta); 8003B1C4 é o laço dos 22 ossos que
chama as nativas 8003B5C0/8003B71C/80037A9C. Tempo próprio: 8003A87C fora da lista dos 40
maiores, 8003B1C4 0,9–2,7%. Com o perfil exato:

| rotina | tempo | o que é |
|---|---|---|
| 8007E0B8 | 14–15% | DrawOTag: o DMA2 da lista, onde a lista vira pacotes do GS |
| 80036CAC (nativa) | 11,5–12% | vértices, 54% dentro do GTE |
| 800296C4 + 8004CE54 | 9–13% | **espera do VBlank** (laço que sorteia números até a interrupção) |
| 80028FD4 | 1,5–4,6% | não identificada (percorre tabelas de 4 entradas de 8 bytes) |

GTE: 10% do tempo, RTPT metade disso (~520 por VBlank, ~545 ciclos cada).

**Pré-busca na lista** (experimento, tirado do código). A 1ª passada pela lista (Brent) já
anda por todos os nós; os endereços ficavam guardados e, ao desenhar o nó k, `pref` no nó
k + N. Bancada, mediana dos ciclos da GPU por polígono: sem 823/823, com 2 linhas e N=4
835/843, com 1 linha e N=8 849/824 (a mesma versão sem pref deu 837 noutra rodada: a
variação é ±15). Nenhum ganho medível. A tabela de endereços (até 16 KB) disputa o cache
de dados de 8 KB com os próprios nós. O diff ficou só nesta nota: a ideia está acima.

**RTPT com os dois multiplicadores** (`gte.c`, `prod3_par`/`mul32_par`). O EE tem dois
multiplicadores (MULT/MADD em HI/LO e MULT1/MADD1 em HI1/LO1); X e Y de cada vértice saem ao
mesmo tempo, e também SX e SY. `PMFHL.LW` junta os dois resultados num registrador de 128
bits e `PCPYUD` traz a metade de cima. Nada mais no ELF usa o multiplicador 1 (objdump: 0
mult1/madd1/div1/mfhi1), então não há quem mexa no HI1/LO1 no meio. O GCC não tem clobber
para HI1/LO1: o `make` confere isso a cada build (`build/mult1_ok`: o número de instruções
do multiplicador 1 no ELF tem que ser o de `gte.o`; senão o build para). Console: RTPT 543 → 525
ciclos (−3%), e 516 depois de tirar o early-clobber das saídas (revisão); autoteste do boot (rápido x completo, 20 mil casos) com 0 diferenças, agora
também na linha `gte:` do log (`rapido 1, autoteste 0 dif`), porque o começo do log se
perde pela rede.

**Resultado na luta** (bancada, rede, quadros em 1 VBlank): A (V59) 81,5% e 83,4%; com as
duas mudanças 82,9% e 83,3%; só RTPT 83,3% e 83,3%. Dentro da variação. Por janela de 2 s
ficam ~10 quadros de 3+ VBlanks (leituras síncronas do CD pela rede, que pelo pendrive são
assíncronas) e ~7 de 2 VBlanks.

Conclusão: o que sobrava da lista não move os quadros. O tempo que falta está espalhado
(lista 15%, vértices 12%, lógica). O próximo passo de verdade seria tirar a conversão da
lista do caminho do jogo (fazer em paralelo com a lógica do quadro seguinte), uma mudança de
estrutura, não de rotina. Antes disso, medir pelo pendrive com o painel (QUADROS/S): na V57
já dava 59 contra o Paul e 49–60 contra a Xiaoyu, e a V59 subiu ~5 pontos pela rede.

**Bancada:** desde 22/11 o PS2 liga no uLaunchELF, não no PS2LINK. `~/cap_tmp/volta_ps2link.sh`
aperta o reset e abre `mass:/APPS/PS2LINK/BOOT.ELF` pelo navegador do uLaunchELF; o
`bench_luta.sh` e o `teste_final.sh` usam esse script. Um ELF morto pelo `timeout` deixa o
PS2LINK sem responder ("accepted the connection but does not answer"): só o reset resolve.
Nas ~10 partidas pela rede de 23/11 o travamento antes do vídeo (bench_b2, V59) não voltou.

### V60: vale adiar a conversão da lista para o quadro seguinte? (23/11)

`make TRAB_QUADRO=1` (com log): linha `trab:` com trabalho/período de cada quadro em
décimos de ms. Ocioso = tempo na espera do quadro (800296C4) menos as exceções do PS1
que caem nela (o Tekken roda parte da lógica nas interrupções enquanto o laço principal
espera); trabalho = período menos ocioso. Uma tentativa anterior que media só do fim da
espera ao começo da seguinte dava 6,5 ms de média: errada, pelo mesmo motivo.
`tools/trab_quadro.py LOG` cruza os dois. O trabalho sai um pouco menor que o real: 80029A28
(chamada na entrada da espera) e a decodificação da música rodam na espera fora das
exceções e contam como ocioso. A duração dos quadros (1/2/3+ VBlanks) é exata.

Luta no ARCADE pela bancada (rede), 3.534 quadros:

| período | quadros | trabalho |
|---|---|---|
| 1 VBlank | 3.062 | 1.351 abaixo de 12 ms, 655 entre 12–14, 1.027 entre 14–16 |
| 2 VBlanks | 132 | 101 acima de 17 ms (falta de processador), 19 abaixo de 12 |
| 3+ VBlanks | 340 | leitura síncrona do CD pela rede (não existe pelo pendrive) |

Tirando os do CD: 3,7% dos quadros passam de ~16,3 ms de trabalho, quase todos sozinhos
(111 isolados, 3 pares). Modelo de adiar a conversão (até 3,4 ms) para a folga do quadro
seguinte: 3,7% → 2,3% dos quadros perdidos. Ou seja, pelo pendrive a luta já deve ficar
em ~96% dos quadros a 60/s, e o adiamento (~1 dia de trabalho) levaria a ~98%: pouco para
o risco. Quadros de 14–16 ms são comuns (1/3 do total): qualquer cena um pouco mais
pesada passa do limite. Não feito; decidir depois da medida pelo pendrive.
