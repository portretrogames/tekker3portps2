# Changelog — ps2-port-kit

## 0.3.3 — 22/11/2026
- controle: analógico. DualShock posto em modo analógico sem trava, uma vez por
  conexão (padSetMainMode assíncrono, só com o controle estável); sticks em
  `lx/ly/rx/ry` e `analogico` no `kit_controle_estado`;
  `kit_controle_stick_direcional()` dá a máscara (ativa em 1) do direcional do
  stick esquerdo. Pedido recusado pela libpad tentado de novo (até 3 vezes);
  pedido refeito depois de DISCONN, FINDPAD ou ERROR.
- cartao (novo): memory card do PS2 por rom0:MCMAN/MCSERV + libmc (combina
  com o rom0:SIO2MAN/PADMAN do controle). Leitura síncrona; gravação em
  etapas, uma por quadro (`kit_cartao_passo`), sem travar o jogo.
- Usado pelo Tekken 3 V59 (stick esquerdo como direcional nos dois controles;
  save do cartão do PS1 no memory card do PS2, com ícone 3D).

## 0.3.2 — 21/11/2026
- ps1spu: memória do PS1 na SPU2 zerada no início (no console vem com lixo; o
  PCSX2 zera); último bloco limitado ao fim; bloco recusado conta em `erros`.
- ps1spu: a espera de uma amostra entre gravações de KON/KOFF vale também entre
  pacotes (o port pode mandar um pacote na hora do KON).
- Usado pelo Tekken 3 V54.

## 0.3.1 — 21/11/2026
- audsrv: `static u16` com valor inicial deixava o FileSiz do IRX não múltiplo
  de 4 e o IOP do console travava ao carregar (o PCSX2 aceita). Inicializado em
  tempo de execução, e o `kit.mk` recusa IRX com FileSiz desalinhado.

## 0.3.0 — 21/11/2026
- SPU do PS1 tocada na SPU2 (`kit_spu1_*` em `kit/som.h`, `iop/audsrv/src/ps1spu.c`):
  o port manda, uma vez por quadro, um pacote com as escritas do jogo nos
  registradores da SPU e as amostras ADPCM; o audsrv aplica no núcleo 0 da SPU2
  (vozes 0-23, memória do PS1 a partir de 0x80000, afinação × 44100/48000) e
  devolve ENDX/ENVX. Servidor RPC próprio (0x8708850) numa thread e fila
  próprias: o SIF RPC aguenta uma chamada em andamento por servidor, e com os
  dois servidores na mesma fila a resposta do PCM se perdia (PCSX2).
- Reverberação ainda não (os registradores ficam guardados no IOP).
- Usado pelo Tekken 3 (V53): efeitos e vozes. Testado no PCSX2; PS2 físico: pendente.

## 0.2.1 — 21/11/2026
Correções achadas na revisão do port do Castlevania SOTN (que copiou o kit 0.2.0) e
conferidas no Tekken 3 (V52):
- diagnóstico: aviso de bytes descartados gravado direto no arquivo (antes ficava
  preso, porque só há perda com o buffer cheio); escrita parcial continua em vez de
  dar erro; erros com o `errno`; caminho do log longo demais recusado.
- armazenamento: `host:`/`hostN:` reconhecido pelo `:` (antes qualquer "host...");
  reset e sincronização do IOP com prazo de 10 s (antes laços sem fim); sem a espera
  de 100 ms depois da última volta de `kit_arm_esperar`.
- controle: `kit_controle_iniciar` chamado de novo não recarrega o PADMAN.
- som: ligação ao servidor RPC do audsrv com prazo de 2 s.
- audsrv (IOP): bloco do reamostrador que passa do fim da fila circular montado
  contíguo (taxas abaixo de 48 kHz liam além do fim; a 48 kHz nada muda); falha da
  thread de reprodução desfaz e devolve erro; `audsrv_quit` permite iniciar de novo;
  `alloc_new_sample` sem memória devolve erro; pacote PCM sem cabeçalho não liga a
  reprodução; faixas do CDDA: a faixa N toca de `tracks[N-1]` a `tracks[N]` (a faixa 1
  não tocava).
- Fica para depois: avanço fracionário do reamostrador (11025 Hz estéreo avança 470
  bytes, meio quadro); IRX lançado de `mc0:`/`hdd0:` (o reset deixa só o USB).
- Testado: PCSX2 (host e USB forçado, música XA a 48 kHz sem faltas). PS2 físico: pendente.

## 0.2.0 — 20/11/2026
- Módulo som (`kit/som.h`): saída PCM pelo audsrv (freesd + audsrv embutidos;
  o audsrv é compilado de `iop/audsrv`, cópia do port SMW PS2 com o volume do
  PCM corrigido). Usado pela música XA do Tekken 3 (V47).
- Envio sem esperar o IOP (`kit_som_concluir` / `kit_som_mandar`, cliente RPC
  próprio com `SIF_RPC_M_NOWAIT`): no PCSX2 o VBlank do Tekken foi de ~210 us
  (2 RPCs que esperam a resposta) para ~19 us.
- Testado: PCSX2 (host e captura do SPU2 em WAV). PS2 físico: pendente.

## 0.1.0 — 15/11/2026
- Primeira versão, extraída do port do Tekken 3 (V17):
  controle (libpad, 2 portas), armazenamento (host:/USB, IRX embutidos,
  espera do pendrive), diagnóstico (log em arquivo, relógio do EE).
- Testado: host (compilação), PCSX2 (caminho USB forçado com FORCAR_IOP).
  PS2 físico: pendente.
