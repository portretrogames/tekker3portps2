# Tekken 3 (port nativo) — como rodar no PS2 de verdade

## O que vai no pendrive (FAT32 ou exFAT)

```
APPS/
  TEKKEN3/
    tekken3.elf        <- gerado por "make pacote" (build/pacote/APPS/TEKKEN3)
    title.cfg          <- para o OPL listar em "Apps"
    TEKKEN3.BIN        <- faixa 1 do SEU disco (ou "Tekken 3 (USA) (Track 1).bin")
```

- Só a **faixa 1** (dados) é necessária por enquanto (o som ainda não existe).
- O `SLUS_004.02` **não precisa** ser extraído: o port lê o executável de
  dentro do `.bin` (segue o `SYSTEM.CNF`, como a BIOS). Se existir um
  `SLUS_004.02` ao lado do ELF, ele é usado no lugar.
- Também funciona com tudo em `mass:/TEKKEN3/`.

## Como abrir

- **OPL**: aba *Apps* → "Tekken 3 (port nativo)".
- **wLaunchELF**: navegue até `mass:/APPS/TEKKEN3/tekken3.elf` e abra.

## O que acontece na partida

1. O ELF reinicia o IOP e carrega os módulos embutidos nele: iomanX,
   fileXio, usbd, bdm, bdmfs_fatfs (FAT32/exFAT), usbmass_bd.
2. Espera o pendrive aparecer (até 10 s).
3. Carrega SIO2MAN/PADMAN da BIOS do PS2 (controle na porta 1).
4. Abre o `.bin`, carrega o executável do PS1 e começa o jogo.

No PCSX2 (ELF aberto de `host:`) o IOP não é reiniciado. Para testar no
PCSX2 o caminho do PS2 real: `make FORCAR_IOP=1`.

## Log e painel (para diagnóstico)

- **Log:** o port grava `tekken3_log.txt` na mesma pasta do ELF (descarrega a
  cada ~2 s). Depois de jogar, copie o arquivo para o PC e mande para análise.
  As linhas `csv:` têm: tempo (ms do EE), % do EE na GPU, % em interrupção,
  listas de desenho, listas puladas, polígonos por VBlank (a cada 120 VBlanks).
- **Painel na tela:** canto superior esquerdo, mesmos números. **L3 + R3**
  liga/desliga. A última linha mostra o log: `LOG OK <bytes gravados>` ou
  `LOG ERRO <passo> <código>` (CRIAR, ABRIR, ESCREVER, FECHAR, STREAM).
- O log fica na RAM e a cada ~2 s é gravado com abrir → acrescentar →
  fechar (runtime/diag_log.c), sem depender de freopen.
- Se a GPU passar de 50 % do tempo, quadros inteiros deixam de ser desenhados
  (a lógica do jogo continua na velocidade certa).

## Limitações atuais

- Sem som; sem memory card (o jogo segue sem salvar).
- Controle digital (sem analógico/vibração).
- Imagem desenhada pelo GS; vídeo de 640×448 entrelaçado (NTSC).
