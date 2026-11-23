# Fase 2 — Etapa 2: ELF que carrega o jogo

## Situação em 14/11/2026

- `make` gera o **tekken3.elf** (text 5,75 MB, bss 2,1 MB) e `make test`
  passa (RAM e memória).
- O `main` carrega o **SLUS_004.02** (PS-X EXE) na RAM do PS1:
  1.183.744 bytes (0x121000) em 0x80010000, PC=0x80079C70, SP=0x801FFFF0,
  GP=0. Procura o arquivo ao lado do ELF, depois em `host:` e
  `mass:/TEKKEN3/`.
- **Testado no PCSX2 2.9.179 (Linux, sem tela, BIOS scph39001)**: o ELF
  inicia, carrega o jogo e executa o código de inicialização do Tekken 3.

## Primeiro resultado (14/11/2026)

```
Tekken 3 PS2 - Fase 2 (runtime minimo)
loader: host:SLUS_004.02 carregado: 1183744 bytes em 80010000, PC=80079c70
Entrando no jogo em 80079c70 (sp=801ffff0 gp=00000000)
PS2 memory: escrita 16 bits em 0x1F801074 (valor 0x00000000) nao implementado
```

`0x1F801074` é o **I_MASK** (controlador de interrupções do PS1). O jogo
zera a máscara logo no começo → **próxima etapa: I_STAT/I_MASK e timers**.

### Correção necessária: laço de despacho (CPS)

O código gerado funciona em estilo CPS: cada função termina gravando o
próximo endereço em `cpu->pc` ("CPS jal", "CPS: jr $ra") e retorna. Sem um
laço, o jogo "voltava" depois da primeira função (pc=80028BA0). O `main`
agora roda `psx_dispatch_game_compiled(cpu, cpu->pc)` em laço e para com log
quando o endereço não tem código compilado.

## Memória (o que mudou)

- `ram.h`: acesso direto (PS1 e EE são little-endian; PS1 exige
  alinhamento). Nada de montar byte a byte. Endereço desalinhado é alinhado
  para baixo em vez de travar o EE.
- `fast_memory.h` (só no código gerado): leituras **e escritas** na RAM são
  inline. As escritas usavam ponteiro de função (`cpu->write_word`); uma
  macro troca por chamada inline sem editar `gerado/`. Num arquivo de teste,
  as chamadas indiretas caíram para 2.
- `memory.c`: scratchpad de 1 KB (só KUSEG/KSEG0, como no PS1) e I/O. Acesso
  a endereço não implementado **para** com o endereço no log.
- Custo: o código do jogo passou de ~3,9 MB (sem inline) para ~5,6 MB. Cabe
  folgado nos 32 MB; desempenho ainda não medido.

## O que esperar no primeiro teste

O jogo deve parar logo no começo, em um destes pontos (é o esperado):

1. `PARADA: salto sem codigo compilado em 000000A0/B0/C0` — chamada de
   função da BIOS do PS1 → próxima etapa: ligar o OpenBIOS recompilado.
2. `PARADA: syscall sem BIOS` — mesma causa.
3. `PS2 memory: ... em 0x1F80xxxx nao implementado` — acesso a hardware
   (GPU, SPU, CD, timers) → implementar esse dispositivo.

O endereço do log decide a próxima etapa.

## Como testar

Automático (Linux/WSL, sem tela): `bash ps2/tools/rodar_pcsx2_linux.sh 40`
— grava `docs/ultimo_log_pcsx2.txt` e `build/tela_pcsx2.png`.

No Windows:

1. No WSL, em `ps2/`: `make -j4`.
2. No Windows: dois cliques em `ps2/rodar_pcsx2.bat`.
3. Feche o PCSX2 quando parar; o log fica em `docs/ultimo_log_pcsx2.txt`.

Se nenhuma linha "Tekken 3 PS2" aparecer no log, o ELF nem iniciou —
provável problema da BIOS do PCSX2 (OpenPS2BIOS); use o dump da BIOS do seu
console.
