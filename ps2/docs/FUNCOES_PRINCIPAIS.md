# Tekken 3 (SLUS-004.02) — funções principais

Análise de 17/11/2026: executável extraído do disco (`tools/extrair_exe.py`),
REA + Rizin 0.9.1 (MIPS, base 0x80010000), sementes = 2.978 entradas do despacho
do recompilador; 2.176 funções reconhecidas, 6.416 chamadas diretas. Papéis
identificados por strings referenciadas, desmontagem e perfil do port no console.

**19/11/2026:** nomes de biblioteca conferidos no Ghidra 12.2 com o ghidra_psx_ldr,
que achou a PsyQ **4.1** no binário e reconheceu 404 funções pelas assinaturas
(projeto e listas em `Codigo/referencia/ghidra_proj`, fora do git; refazer:
`INSTRUÇÕES IA/GUIA-ENGENHARIA-REVERSA.md`). Quatro nomes daqui estavam errados e
foram corrigidos: `8007e0b8`, `80088ba8`, `8004cc08` e `8004b39c`.

## Estrutura do jogo (laço principal e ritmo)

| Endereço | Nome sugerido | O que faz | Evidência |
|---|---|---|---|
| 80079c70 | `_start` | Entrada do EXE: zera o BSS (800995a8–800b0548), monta a pilha e chama o jogo | cabeçalho PS-X EXE (pc0) |
| 800296c4 | `EsperaQuadro` | Laço que espera a flag 8009bc5c (chamando 8004ce54) — fim de cada quadro | Ghidra + perfil V37 |
| 80029784 | `TickTimer2` | Handler da IRQ do Root Counter 2 (OpenEvent 0xF2000002): libera o próximo passo (8009bc5c++), conta 80095434 | desmontagem V35 |
| 8002ae58 | `AtualizaLuta` | Lógica da luta (personagens, colisão, câmera); 2× por quadro; 252 chamadas diretas | Ghidra + perfil |
| 80028ba0 | `main` | Laço principal (chama 800296c4 e 8006b6fc a cada quadro) | ghidra_psx_ldr + ANALISE_V42_V43 |
| 8007c998 | `DrawOTag` (libgpu) | Põe na fila da libgpu a rotina 8007e0b8 com a OT: chama a função da tabela do driver `80098B70`+8 (`8007e154`, a fila) passando o ponteiro de `80098B70`+0x18 (`8007e0b8`) | assinatura PsyQ 4.1 + desmontagem |
| 8007e0b8 | interna da libgpu (`SYS.OBJ`+0x2090) | Inicia o DMA2 da ordering table (GP1 0x04000002, CHCR 0x01000401). **Não é o DrawOTag** (nome corrigido em 19/11): é o que ele põe na fila; também chamada por `8007db5c` | assinatura + desmontagem |
| 80036cac | `TransformaModelo` | Rotina 3D pesada (2.920 bytes, 108 blocos); já é C nativo no port (~13% do EE). Provavelmente assembly escrito à mão (compilador não costuma gerar isso): o desvio em 80037094 vai para 800370ac, que é o slot de atraso do desvio em 800370a8, e por isso o Ghidra não a descompila (erro "Instruction at 800370ac overlaps instruction at 800370a8"; leia a listagem ou o C do `tools/nativo.py`) | perfil V39 + Ghidra 19/11 |
| 80088ba8 | `_SsVmFlush` (libsnd) | Atualiza as vozes do sequenciador de som (laço por voz), chamado na IRQ do timer 2; é o que o port pula (`PS2_SOM_PULAR`) | assinatura PsyQ 4.1 + perfil V35/V36 |
| 80059890 | (desconhecido) | Maior função de código (31 KB, 1.717 blocos) e a MAIS chamada (377 chamadores) — provável interpretador de golpes/scripts; o Rizin pode ter juntado várias funções | Rizin |

## Telas e modos (por strings)

| Endereço | Função | Strings |
|---|---|---|
| 8003dcc8 | Anúncio do round | "ROUND %1x", "FINAL ROUND" |
| 8003dde8 | Anúncio de fase (modos 1P) | "STAGE %1x", "FINAL STAGE", "READY?" |
| 8003df68 | Fim do round | "K.O.", "DOUBLE K.O.", "PERFECT!", "TIME UP" |
| 8003e170 | Resultado da luta | "%s WINS!", "DRAW", "YOU WIN!", "YOU LOSE" |
| 8003dd94 / 8003dda8 / 8003ddc8 | Títulos de modo | "TEAM BATTLE", "SURVIVAL BATTLE", "PRACTICE MODE" |
| 80032348 | Replay | "REPLAY" |
| 8004f750 | Abertura | "NAMCO PRESENTS." |
| 80055080 | Tela de título | "THE KING OF IRON FIST TOURNAMENT 3" |
| 80054708 / 800548d4 | Seleção de personagem | "PLAYER(S)", "NO ENTRY", "SELECTED" |
| 80055804 | Tela "VS" | "VS" |
| 8004fff0 | Continue | "CONTINUE? %d" |
| 800780bc | Pausa | "%dP PAUSE" |
| 8004ec24…8004eca0 | Menus de modos / pontuação | "TIME ATTACK", "SURVIVAL", "TEKKEN BALL", "SCORE" |
| 8004c138 / 8004c240 / 8004c3b8 / 8004c470 | Memory card (save) | "bu00:", "BASLUS-00402TEKKEN-3", "AUTO SAVE ERROR!" |
| 8006ab64 | Abre arquivos do disco | "\TEKKEN3\TEKKEN3.BNS;1", "\TEKKEN3\TEKKEN3.XAS;1" |

## Bibliotecas PsyQ reconhecidas

Nomes das assinaturas da PsyQ 4.1 (ghidra_psx_ldr, 19/11/2026). Funções internas
(sem nome público) aparecem como `OBJ`+deslocamento. Lista completa em
`Codigo/referencia/ghidra_proj/saida/` (fora do git). **Cuidado com funções curtas**
(≤ 32 bytes: 208 das 404): várias bibliotecas têm "embrulhos" iguais (`f() { g(0); }`)
e o nome pode sair trocado; confira o que a função chama (exemplo: `SpuInit` abaixo).

| Endereço | Função | Evidência |
|---|---|---|
| 8007a318 | `rand` (libc) | assinatura; LCG 0x41C64E6D/0x3039 — 106 chamadores |
| 8007a458 | `printf` (libc) | assinatura; varargs → 8007a498 com fd 1 |
| 8007a3c8 | `strcpy` (libc) | assinatura |
| 8007bb28 | `SetSemiTrans` (libgpu) | assinatura |
| 8007c028 / 8007c998 / 8007cbd4 / 8007ca08 | `ResetGraph`, `DrawOTag`, `PutDispEnv`, `PutDrawEnv` (libgpu) | assinatura |
| 8007c30c / 8007c3a4 / 8007c7c8 | `SetDispMask`, `DrawSync`, `ClearOTag` (libgpu) | assinatura |
| 8007c528 / 8007c5b8 / 8007c650 / 8007c710 | `ClearImage`, `ClearImage2`, `LoadImage`, `MoveImage` (libgpu) | assinatura; "%s:bad RECT" |
| 8007e0b8 / 8007e154 | internas da libgpu (`SYS.OBJ`): DMA2 da OT e fila | assinatura + desmontagem (ver acima) |
| 8007e924 | interna da libgpu (`SYS.OBJ`+0x28FC): timeout da GPU | assinatura; "GPU timeout:que=%d,stat=…" |
| 8007f5b0 / 800800b8 | debug de primitivos (`TMD.OBJ`) | assinatura; "FT3L/GT3L", "FT4L/GT4L" |
| 80079e18 / 80079e48 / 80079ec8 / 80079f9c | `OpenEvent`, `EnableEvent`, `SetRCnt`, `StartRCnt` (libapi) | assinatura |
| 800859a8 / 80085bc8 / 80085c58 | `VSync`, `ResetCallback`, `VSyncCallback` (libetc) | assinatura |
| 80081c50 / 80081cd8 | `InitGeom`, `SquareRoot0` (libgte) | assinatura |
| 80081d68 | `MulMatrix0` (libgte) — já é C nativo no port (`tools/nativas.txt`) | assinatura |
| 80082288 / 800824a8 / 80082638 / 800826c8 | `MulMatrix`, `ApplyMatrix`, `SetRotMatrix`, `SetTransMatrix` (libgte) | assinatura |
| 800828a8 | `ratan2` (libgte) | assinatura |
| 800833a8…800846d0 | libcd (`BIOS.OBJ`): `CD_sync` 80083904, `CD_ready` 80083b84, `CD_cw` 80083e4c, `CD_vol`, `CD_flush`, `CD_initvol`, `CD_init` 800844f0 | assinatura; "CD_sync", "CD_init:", "CD timeout" |
| 80083850 | interna da libcd (`BIOS.OBJ`+0x4A8): interrupção do CD | assinatura; "CDROM: unknown intr" |
| 8008af48 | `SpuInit` (libspu) | desmontagem: chama `_SpuInit` (8008af68, assinatura) com 0. O Ghidra deu o nome `SpuInit` a **8004b770** por engano: essa chama `SsUtKeyOffV(0)` (80088938) |
| 80088ba8 | `_SsVmFlush` (libsnd) | assinatura (ver acima) |
| 8008b0c8 / 8008b348 | `_spu_init` e interna (`SPU.OBJ`+0x280) (libspu) | assinatura; "wait (reset)", "wait (dmaf clear/W)" |
| 80091558 | `DsSearchFile` (libds) | assinatura |
| 80091858 / 80092608 | internas da libds (`DSFILE.OBJ`, `DSTYPE.OBJ`): leitura do ISO9660 | assinatura; "CD001" |

Do jogo (sem assinatura), com nomes antigos corrigidos: `8004cc08` copia bytes até
0, mas **não é** o `strcpy` da libc; `8004b39c` faz testes de sinal/quadrante, mas
**não é** o `ratan2` da libgte.

## Mais chamadas (top)

80059890 (377), rand 8007a318 (106), 8002f3ac (71, 7 KB), 80056764 (65), 8004ce74 (63, printf de texto na tela),
printf 8007a458 (61), 8002e0bc (57), 8004b39c (55), 8006f5c0 (46), 80077724 (45), 8004cc08 (44, cópia até 0),
SetSemiTrans 8007bb28 (44).

## Limitações

- Binário bruto sem símbolos. Nomes de biblioteca: conferidos por assinatura
  (PsyQ 4.1). Nomes de funções do jogo: sugeridos por evidência, não confirmados.
- 800c2dbc (106 KB) e 800c2434 (50 KB) são dados que o Rizin tratou como código.
- Chamadas indiretas (ponteiros de função, como o próprio TickTimer2 via OpenEvent) não aparecem como chamadores.
