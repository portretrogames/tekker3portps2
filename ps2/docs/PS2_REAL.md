# Tekken 3 (port nativo) — como rodar no PS2 de verdade

## O que vai no pendrive (FAT32 ou exFAT)

```
APPS/
  TEKKEN3/
    tekken3.elf                     <- "make pacote" (build/pacote/APPS/TEKKEN3)
    title.cfg                       <- para o OPL listar em "Apps"
    Tekken 3 (USA) (Track 1).bin    <- faixa 1 do SEU disco (ou TEKKEN3.BIN)
    Tekken 3 (USA) (Track 2).bin    <- faixa 2 (CD-DA), opcional
ART/
    tekken3.elf_COV.png ...         <- artes do OPL (tools/gerar_artes_opl.py)
```

- O `SLUS_004.02` **não precisa** ser extraído: o port lê o executável de
  dentro do `.bin` (segue o `SYSTEM.CNF`, como a BIOS). Se existir um
  `SLUS_004.02` ao lado do ELF, ele é usado no lugar.
- Também funciona com tudo em `mass:/TEKKEN3/`.
- Sem a faixa 2 o jogo roda igual; o trecho em CD-DA fica em silêncio.

## Como abrir

- **OPL**: aba *Apps* → "Tekken 3 (port nativo)".
- **wLaunchELF**: navegue até `mass:/APPS/TEKKEN3/tekken3.elf` e abra.

## O que acontece na partida

1. O ELF reinicia o IOP e carrega os módulos embutidos nele: iomanX,
   fileXio, usbd, bdm, bdmfs_fatfs (FAT32/exFAT), usbmass_bd, freesd e audsrv.
2. Espera o pendrive aparecer (até 10 s).
3. Abre os dois controles (DualShock em modo analógico) e o memory card da
   porta 1.
4. Mostra as telas de início (X ou START seguem) e começa o jogo.

## Memory card

O save do Tekken 3 fica no memory card do PS2 (porta 1), em
`mc0:/TEKKEN3PS2/` (com ícone 3D no navegador do PS2). Use CARD SAVE / CARD
LOAD no menu OPTION do jogo; o jogo também lê o save sozinho ao ligar.

## Controles

Dois controles. O analógico esquerdo funciona como o direcional.

## Limitações

- A luta fica a 60 imagens/s na maior parte do tempo; em cenas pesadas um
  quadro ou outro cai para 30.
- Sem vibração.
- Vídeo de 640×448 entrelaçado (NTSC).
