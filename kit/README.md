# ps2-port-kit — bibliotecas compartilhadas dos ports para PS2

Módulos em C para ports nativos de jogos para o PlayStation 2 (ps2sdk).
Começou no port do Tekken 3; a ideia é reusar no Yu-Gi-Oh, no Mario e nos
próximos. **O kit não sabe nada de jogo nenhum**: nomes de arquivos, pastas
e versões ficam no projeto (no Tekken: `ps2/runtime/jogo.h`).

Hoje está dentro do repositório do Tekken (`kit/`). Plano: virar um
repositório próprio (`ps2-port-kit`) usado como submódulo git — ver
`../ps2/docs/PLANO_KIT.md`.

## Módulos (etapa 1 — camada "base PS2")

| módulo | header | o que faz |
|---|---|---|
| controle | `kit/controle.h` | libpad: abre 1 ou 2 portas, lê botões (ativo em 0, layout PS1/PS2), estado de conexão; 0.3.3: DualShock em modo analógico (sem trava) e sticks `lx/ly/rx/ry` |
| cartao | `kit/cartao.h` | 0.3.3: memory card do PS2 (rom0:MCMAN/MCSERV + libmc): leitura síncrona; gravação em etapas sem travar o jogo, por `~NOME` (o rom0:MCSERV não renomeia) |
| armazenamento | `kit/armazenamento.h` | caminho `host:` (PCSX2) ou USB do console: reinicia o IOP, carrega iomanX, fileXio, usbd, bdm, bdmfs_fatfs (FAT32/exFAT) e usbmass_bd embutidos no ELF, espera o pendrive montar |
| diagnóstico | `kit/diagnostico.h` | log em arquivo no pendrive (printf → buffer na RAM, abre/acrescenta/fecha quando o jogo pede, no Tekken a cada ~30 s), estado curto para painel, relógio do EE |
| som | `kit/som.h` | PCM 16 bits estéreo pelo audsrv: carrega o freesd e o audsrv do kit (`iop/audsrv`, o do PS2SDK com o volume do PCM corrigido, vindo do port SMW PS2; LGPL-2) embutidos no ELF; envia sem nunca esperar o IOP e completa com silêncio (o IOP repete o buffer dele se ninguém mandar nada) |

Próximas etapas (ainda no projeto do Tekken, `ps2/runtime/`):
- camada PS1: GPU→GS, GTE, MDEC, CD, BIOS HLE, IRQ/timers/DMA, RAM;
- painel de texto na tela e vídeo gsKit (hoje em `gs_render.c`/`video_gs.c`).

## Como usar

No Makefile do projeto (ps2sdk), antes de montar `EE_OBJS`:

```make
KIT_DIR := ../kit
KIT_BUILD := build
include $(KIT_DIR)/kit.mk
EE_OBJS += $(KIT_OBJS)
EE_INCS += $(KIT_INCS)
EE_LIBS += $(KIT_LIBS)
```

Exemplo mínimo (sem assets de jogo nenhum): `exemplo/main.c`.

```c
kit_arm_pasta_do_elf(argv[0], pasta, sizeof(pasta));
if (kit_arm_preparar(argv[0], 0) == KIT_ARM_OK)      /* console: USB */
    kit_arm_esperar(caminhos, 10000, NULL, 0);        /* espera montar */
kit_log_iniciar(caminho_do_log);                      /* printf -> log */
kit_controle_iniciar(1);
/* a cada quadro: */ kit_controle_ler(0, &estado);
/* de tempos em tempos (CD parado): */ kit_log_gravar();
```

## Dependências e limitações

- ps2sdk (libpad, libfileXio, libpatches, libaudsrv); os IRX vêm de `$(PS2SDK)/iop/irx`, menos o
  audsrv, compilado de `iop/audsrv` pelo `kit.mk` (precisa do toolchain do IOP, `mipsel-none-elf`).
- Controle: só digital (sem analógico/vibração).
- Armazenamento: só USB (sem HDD/MX4SIO); a espera é por polling de `fopen`.
- Log: buffer de 256 KB entre gravações (`KIT_LOG_BUF`); o excedente é descartado, contado e
  avisado no arquivo na gravação seguinte.
- Quem usa leitura assíncrona do fileXio deve registrar
  `kit_log_antes_de_gravar()` para terminar a leitura antes de o log usar o
  fileXio (o Tekken faz isso com o CD).

## Testes

Registrar sempre onde foi testado: **host** (gcc no PC), **PCSX2** e
**PS2 físico** — o PCSX2 não prova o console.

## Origem e licença

Código escrito para os ports da Retrogames (uso livre e não comercial, como
os projetos que o usam). Base dos exemplos do ps2sdk (`samples/rpc/pad`).
Não contém dump, código recompilado nem nada de jogo.
