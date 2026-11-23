# Fase 2 — Etapa 3: interrupções, timers, DMA e BIOS mínima

## Resultado (14/11/2026, PCSX2 2.9.179 Linux, BIOS scph39001)

O Tekken 3 roda a inicialização, recebe o **VBlank a 60 Hz** pelo handler
dele e chega na **biblioteca de CD da Sony**, que fica esperando o drive:

```
bios: B0(19h) SetCustomExitFromException   a0=80099bd8
bios: B0(5Bh) ChangeClearPad
bios: C0(0Ah) ChangeClearRCnt              a0=00000003
bios: A0(72h) _96_remove
bios: B0(35h) write
CD_init:addr=80099a34                      <- texto do próprio jogo
bios: B0(17h) ReturnFromException
vblank: 2400 (entregues ao jogo: 2399)
CD timeout: CD_cw:(CdlNop) Sync=NoIntr, Ready=NoIntr
CD timeout: CD_cw:(CdlReset) Sync=NoIntr, Ready=NoIntr
```

Desempenho do laço de despacho no PCSX2: ~470 mil despachos/s (ainda sem
medida de FPS do jogo).

## O que foi implementado

- `runtime/io.c`: registradores 0x1F801000-0x1F802FFF.
  - I_STAT/I_MASK (interrupções), timers 0-2 (relógio do PS1 derivado do
    COUNT do EE: x 7527/65536), DMA com conclusão imediata e canal 6 (OTC)
    real, GPUSTAT sempre pronto, SPUSTAT espelhando SPUCNT.
  - Qualquer outro registrador: guarda o valor e registra o 1º acesso
    (`io: 1a leitura/escrita`) — assim um teste mostra todo o hardware usado.
- `runtime/bios_hle.c`: BIOS do PS1 em C (A0/B0/C0 pelo número em t1).
  - Implementadas: SetCustom/DefaultExitFromException, ReturnFromException,
    ChangeClearRCnt, write/putchar (texto do jogo vai para o log).
  - Demais: registradas no log e devolvem 0.
  - **Interrupções**: VBlank a 59,94 Hz; com IRQ pendente e IEc/IM2 ligados,
    salva os registradores, carrega o jmp_buf do jogo (ra, sp, fp, s0-s7,
    gp) e volta ao laço de despacho com longjmp. ReturnFromException
    restaura tudo e retoma no ponto interrompido.
- `stubs.c`: syscall 1/2 (Enter/ExitCriticalSection).
  `psx_check_interrupts_at` consulta o relógio a cada 256 chamadas.

## Hardware acessado pelo jogo até aqui

```
io: 1a escrita I_MASK        1f801074 = 00000000
io: 1a leitura I_MASK        1f801074 = 00000000
io: 1a escrita I_STAT        1f801070 = 00000000
io: 1a escrita DMA DPCR      1f8010f0 = 33333333
io: 1a escrita TIMER         1f801114 = 00000107
io: 1a escrita DMA DICR      1f8010f4 = 00000000
io: 1a escrita CDROM         1f801800 = 00000002
io: 1a escrita CDROM         1f801802 = 00000000
io: 1a leitura CDROM         1f801803 = 00000020
io: 1a escrita MEMCTRL       1f801020 = 00001325
io: 1a leitura GPU GP1/STAT  1f801814 = 1c000000
io: 1a leitura TIMER         1f801110 = 00000035
io: 1a leitura I_STAT        1f801070 = 00000001
io: 1a leitura SPU           1f801db8 = 00000000
io: 1a leitura SPU           1f801dba = 00000000
io: 1a escrita SPU           1f801d80 = 00003fff
io: 1a escrita SPU           1f801d82 = 00003fff
io: 1a escrita SPU           1f801db0 = 00003fff
io: 1a escrita SPU           1f801db2 = 00003fff
io: 1a escrita SPU           1f801daa = 0000c001
io: 1a leitura CDROM         1f801800 = 00000018
```

## Próximo passo

**Controlador de CD-ROM** (0x1F801800-0x1F801803): comandos (GetStat, Init,
Nop, Setloc, ReadN/ReadS, Pause...), respostas pela IRQ2 e leitura de
setores do disco. No PS2 o disco virá do pendrive (imagem .bin/.iso).
