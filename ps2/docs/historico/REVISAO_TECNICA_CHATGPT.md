# Revisão técnica — Tekken 3 PS1 → PS2

14/11/2026, America/Sao_Paulo. Revisão somente de leitura do projeto em **D:\PROJETOS\tekken 3 ps2**. Nenhum fonte, commit, configuração ou processo do projeto foi alterado por esta revisão.

**Conclusão:** a base recompilada compila para o Emotion Engine, mas ainda é um protótipo de runtime híbrido. Foram reproduzidos **sete defeitos de comportamento e uma falha de dependências de build**, além de identificadas lacunas previstas para controle físico, áudio e inicialização no PS2 real. A compilação bem-sucedida não demonstra que o jogo seja jogável, nem confirma 60 FPS.

## Escopo, versão e limites

O HEAD consultado foi `8f0662685020e2aa20738290be47762492db3736` — “GPU em software e saida GS experimental”. A revisão inclui as alterações locais presentes na cópia inicial; não é uma revisão apenas desse commit. A árvore original já continha modificações em runtime, testes e documentação.

A análise e os testes usaram uma cópia em `C:\Users\GUILHERME\Documents\Codex\2026-11-14\referenced-chatgpt-conversation-this-is-an\work\review_snapshot`. O manifesto em `evidencias/manifest.json` identifica os arquivos de runtime, testes e documentação copiados por SHA-256. A cópia não é um snapshot transacional do sistema de arquivos; não há garantia de que todos os arquivos correspondam ao mesmo instante.

Na conferência às 19:55, os fontes originais de BIOS, CD-ROM, timers/DMA, GTE, memória, entrada e Makefile permaneciam iguais aos analisados. **gpu.c e video_gs.c mudaram durante a revisão**: foi acrescentado um contador de palavras GP0 e instrumentação de vídeo, incluindo apresentação a cada três VBlanks. Essas diferenças foram inspecionadas, mas a versão posterior do vídeo não foi recompilada nesta revisão. `ultimo_log_pcsx2.txt` estava aberto por outro processo; não foi tratado como registro final.

Não executei uma nova sessão de PCSX2 nem teste em PS2 físico. A tela preta e o histórico de inicialização citados abaixo vêm da documentação e do log já existentes no projeto. Não estabeleci a causa única da tela preta.

## Compilação e evidências executadas

| Verificação | Resultado |
|---|---|
| `make test` na cópia | Os quatro executáveis existentes passaram: RAM, memória/I/O, GTE e GPU |
| `make -j4` com ps2dev | Compilação completa e link concluídos |
| Compilador EE | `mips64r5900el-ps2-elf-gcc`, GCC 15.2.0 |
| Compilador dos testes | GCC Ubuntu 15.2.0 |
| Formato do ELF | ELF32 little-endian; flags R5900/MIPS3; entrada `0x001014b0` |
| Tamanhos de seções | text 5.703.172 bytes; data 6.344; bss 3.852.800; soma 9.562.316 bytes (~9,12 MiB) |
| Segmento carregável | Base `0x00100000`, MemSiz `0x0091ea80`, término `0x00a1ea80` |
| Aviso observado | `main.c:150`: limite de `strncat` depende do comprimento da origem |

O campo genérico `Machine: MIPS R3000` do readelf não torna o ELF um executável PS1: as flags específicas incluem 5900, e a compilação usou a ferramenta do EE.

Logs completos estão em `evidencias/`. Os testes adicionais usam os fontes da cópia, um relógio controlado nos testes de timers e setores sintéticos no teste de CD; não dependem do dump comercial. O UBSan terminou com código 1 **porque detectou o defeito descrito em F7**. Não é uma falha de compilação.

## Achados priorizados

P1 = corrigir antes de confiar no avanço da inicialização e nas transições do jogo. P2 = corrigir para robustez, fidelidade e desenvolvimento confiável. Não foi atribuído P0: não há evidência de um defeito universal que justifique essa classificação.

### F1 — P1: chamadas gráficas da BIOS são aceitas e descartadas

**Local:** `D:\PROJETOS\tekken 3 ps2\ps2\runtime\bios_hle.c:329`, linhas 331–336 e 416–417. A identificação de `GPU_cw` está na linha 20.

`ps2_bios_call` reconhece a tabela A0 e ajusta o retorno ao chamador, mas somente trata A0(AB/AC). A0(49), embora apareça pelo nome no log, não envia a palavra a `ps2_gpu_gp0`. As outras chamadas A0 não implementadas também terminam com v0=0, sem diagnóstico de operação ausente.

**Reprodução:** três chamadas A0(49) com um pacote de preenchimento vermelho deixam o pixel em `0000`. Enviar exatamente as mesmas palavras diretamente a GP0 produz `001f`. Resultado em `evidencias/review_checks.log`. O log histórico do próprio jogo contém A0(49) às 19,205 s, portanto esse caminho é efetivamente alcançado; não prova, isoladamente, que ele seja a causa da tela preta.

**Correção proposta:** implementar A0(48/49/4A/4B/4C) com suas operações e retornos específicos; centralizar o envio nos caminhos GPU/DMA existentes. Para chamadas ainda ausentes, registrar tabela, número, PC, RA e argumentos, distinguindo explicitamente um stub permitido de uma operação não implementada. Não retornar silenciosamente de operações de memória, heap ou controle de fluxo.

**Aceite:** pacote via BIOS e via GP0 deve produzir a mesma VRAM; testar também GP1 e listas DMA. A semântica dessas chamadas está documentada em [PSX-SPX — BIOS GPU](https://psx-spx.consoledev.net/ps1/kernelbios/gpu-functions/).

### F2 — P1: um setor XA após dados pode interromper permanentemente a leitura

**Local:** `D:\PROJETOS\tekken 3 ps2\ps2\runtime\cdrom.c:302`, especialmente linhas 303–318.

Depois de consumir um setor de dados, `setor_ok` permanece 1. O próximo setor XA põe `setor_pedido=0`, mas o caminho XA não limpa `setor_ok`, não entrega um novo INT1 e não libera a condição de avanço. O próximo setor passa a depender de um pedido de dados que o consumidor não tem motivo para fazer.

**Reprodução:** arquivo sintético com dados → XA → dados, modo XA habilitado. Após XA: `lba=2 setor_ok=1 pedido=0`; no próximo instante de leitura, LBA continua 2, em vez de avançar a 3. Evidência: `evidencias/review_cd.log`.

**Correção proposta:** separar estado de setor destinado à CPU, FIFO consumível e setor destinado ao áudio. Um setor XA encaminhado ou descartado deve liberar o avanço sem exigir BFRD. Validar também Setfilter, que atualmente apenas confirma o comando.

**Aceite:** percorrer dados/XA/dados, vários XA consecutivos, Pause/Read e ambos os modos de velocidade, mesmo com saída de áudio desabilitada. Não exigir que o subsistema de áudio esteja pronto para corrigir o fluxo de dados.

### F3 — P1: callbacks entregues fora de uma exceção são perdidos

**Local:** `D:\PROJETOS\tekken 3 ps2\ps2\runtime\bios_hle.c:113`, linhas 119–122, 256 e 341.

`DeliverEvent` enfileira callbacks, mas fora da exceção não inicia sua execução. A próxima entrada em exceção faz `cb_n=0`, apagando a fila antes de processá-la.

**Reprodução:** evento ativo em modo callback, destino `0x80012340`; entrega deixa uma entrada pendente. A próxima exceção remove a entrada e retorna ao PC `0x80010000`, sem executar o callback. Teste de caixa branca em `evidencias/review_bios_callback.log`.

**Correção proposta:** criar uma continuação CPS para callbacks de DeliverEvent, preservando retorno e contexto do chamador; não reutilizar a limpeza da fila de uma nova exceção para apagar entregas pendentes. Validar entrega dentro e fora de handlers e múltiplos callbacks. [Semântica dos eventos na BIOS](https://psx-spx.consoledev.net/ps1/kernelbios/event-functions/).

### F4 — P1: WaitEvent retorna antes de o evento ocorrer

**Local:** `D:\PROJETOS\tekken 3 ps2\ps2\runtime\bios_hle.c:344`, linhas 344–349.

O caso WaitEvent é equivalente ao TestEvent. Com evento habilitado e ainda ocupado, volta imediatamente ao RA com zero. Isso permite que código que espera a conclusão de uma operação prossiga cedo demais.

**Reprodução:** OpenEvent → EnableEvent → WaitEvent sem entrega: `pc=80010000 returned=0`. Evidência em `evidencias/review_checks.log`.

**Correção proposta:** manter uma continuação de espera e continuar atendendo dispositivos/interrupções até o evento ficar pronto; então consumir o estado pronto e retornar 1. Evitar um laço C bloqueante que impeça o scheduler de progredir. Preservar o retorno zero para evento desabilitado. [PSX-SPX — WaitEvent](https://psx-spx.consoledev.net/ps1/kernelbios/event-functions/).

### F5 — P1: timer com IRQ no alvo e sem reset no alvo dispara no momento errado

**Local:** `D:\PROJETOS\tekken 3 ps2\ps2\runtime\io.c:179`, linhas 179–187; estado de modo em 140–141.

O período vira 65.536 quando o reset no alvo está desligado, mesmo que IRQ no alvo esteja ligada. A comparação com o alvo não é calculada independentemente do overflow. Os bits de “atingiu alvo/overflow” também são limpos na leitura, mas não são estabelecidos nesse processamento.

**Reprodução:** timer0 modo `0x50`, alvo 1.000, contador levado deterministicamente a 2.032: IRQ=0, embora o alvo já tenha sido cruzado. Evidência em `evidencias/review_checks.log`.

**Correção proposta:** calcular cruzamentos de alvo e overflow separadamente, incluindo wrap, one-shot/repeat, pulse/toggle e os bits latched de modo. Usar relógio injetável para os testes, sem depender de tempo real. [PSX-SPX — Timers](https://psx-spx.consoledev.net/timers/).

### F6 — P2: DICR anuncia interrupção sem encaminhá-la a I_STAT

**Local:** `D:\PROJETOS\tekken 3 ps2\ps2\runtime\io.c:314`, linhas 314–327.

Uma escrita de DICR pode fazer o bit mestre passar de zero a um, mas `dma_dicr_write` não atualiza IRQ_DMA. O readback calcula esse bit; o encaminhamento de IRQ só acontece no caminho de conclusão de DMA.

**Reprodução:** I_MASK com IRQ_DMA habilitada; escrita `0x00008000` em DICR. Leitura `0x80008000`, porém IRQ pendente=0. Evidência em `evidencias/review_checks.log`.

**Correção proposta:** uma função única deve recalcular o estado mestre e sinalizar a transição 0→1, tanto em escrita quanto em conclusão. Revisar também a interação entre habilitação global, flags persistentes e máscaras por canal; não limitar a correção ao bit 15. [PSX-SPX — DICR](https://psx-spx.consoledev.net/dmachannels/).

### F7 — P2: GTE usa deslocamento à esquerda de inteiros negativos

**Local:** `D:\PROJETOS\tekken 3 ps2\ps2\runtime\gte.c:252`; casos semelhantes em 254, 256, 298, 318–320, 386, 390, 423–424 e 465.

Expressões como `((int64_t)(int32_t)C(5) << 12)` têm comportamento indefinido em C quando o valor é negativo. Translações e componentes negativos fazem parte do domínio normal do GTE. O compilador pode transformar essas expressões de forma incompatível com a intenção aritmética.

**Reprodução:** TRX=-1 seguido de RTPS. UBSan relata `gte.c:252:56: left shift of negative value -1`. Evidência em `evidencias/review_gte_ub.log`. Isso confirma UB no fonte; não comprova uma imagem corrompida específica no PS2.

**Correção proposta:** onde os limites garantirem segurança em 64 bits, multiplicar por 4096 ou pelo fator correspondente. Para operações que representam wrap, usar operações unsigned e extensão de sinal explícita. Validar resultados e flags com vetores negativos, extremos, sf/lm, RTPT, iluminação e MVMVA antes de portar operações ao VU0.

### F8 — P2: alterações em headers podem deixar objetos antigos no ELF

**Local:** `D:\PROJETOS\tekken 3 ps2\ps2\Makefile:31`, linhas 31–37.

As dependências são listadas manualmente e omitem, entre outros, `gpu.h`, `video_gs.h` e dependências transitivas dos headers de referência. Não há geração/inclusão de arquivos `.d`.

**Reprodução:** após o build completo, `make -n -W runtime/gpu.h all` responde `Nothing to be done for 'all'`. Alterar o contrato da GPU pode, portanto, deixar chamadores compilados com a declaração antiga.

**Correção proposta:** `-MMD -MP` nas compilações e inclusão dos `.d` correspondentes aos objetos. Separar a execução dos testes de PC dos includes obrigatórios do ps2sdk, para permitir testá-los sem instalar o toolchain EE. O número fixo de 45 shards também deve vir de um manifesto da geração, com verificação de arquivos faltantes/obsoletos.

**Aceite:** simular a mudança de cada header público e conferir quais objetos serão recompilados; comparar um build limpo com o incremental.

## Arquitetura: o que já é nativo e o que ainda é compatibilidade

O caminho principal em `D:\PROJETOS\tekken 3 ps2\ps2\runtime\main.c:175` é um trampolim CPS: tenta código recompilado, depois BIOS HLE e depois `ps2_interp_run`. O código gerado compila para instruções do EE. A RAM PS1 e os registradores de CPU continuam representados em estruturas do runtime.

`D:\PROJETOS\tekken 3 ps2\ps2\runtime\interp.c:60` decodifica opcodes R3000A; as linhas 181–201 executam instruções em laço. Isso é interpretação de CPU. Logo, a descrição “sem emulador” no README é mais ampla do que a implementação atual permite afirmar. A descrição precisa é **recompilação estática para EE com fallback interpretado e compatibilidade de hardware em software**. Isso não impede chegar ao objetivo de port nativo.

Para eliminar o fallback, catalogar overlays por conteúdo/hash e faixa de carga, recompilar cada variante antecipadamente e selecionar a variante válida no despacho. O bitmap de código sujo em `D:\PROJETOS\tekken 3 ps2\ps2\runtime\ram.h:38` e a comparação em `D:\PROJETOS\tekken 3 ps2\ps2\runtime\codigo.c:28` são um ponto de partida útil. Contabilizar entrada, PC e hash de cada fallback; cobertura deve incluir personagens, estágios, modos, finais e transições. Um único menu carregado não demonstra cobertura.

O interpretador ignora load delay e exceções de overflow, conforme `D:\PROJETOS\tekken 3 ps2\ps2\runtime\interp.c:5`. Essas são limitações declaradas, sem prova nesta revisão de que uma instrução específica do jogo dependa delas. Validar as sequências efetivamente usadas antes de tratar a interpretação como oráculo de fidelidade.

## Renderização e desempenho

`D:\PROJETOS\tekken 3 ps2\ps2\runtime\gpu.c:185` rasteriza triângulos no EE. Há cinco interpolações com divisão de 64 bits por pixel coberto nas linhas 213–215. `D:\PROJETOS\tekken 3 ps2\ps2\runtime\video_gs.c:61` na cópia inicial apresenta uma textura já rasterizada. Assim, **o GS recebe a imagem final; ainda não recebe os polígonos do jogo como backend principal**.

A versão inicial usa uma textura de 640×512×2 bytes: 655.360 bytes por envio, aproximadamente 39,3 MB/s se enviada 60 vezes por segundo, além da cópia CPU, limpeza de cache e custo do rasterizador. É uma estimativa pelo tamanho do buffer, não medição de FPS ou saturação do barramento. A versão posterior vista durante a revisão limita a apresentação a um em cada três VBlanks; isso reduz a taxa visual para aproximadamente 20 apresentações/s se houver 60 VBlanks/s. Não deve ser confundido com velocidade da lógica.

A apresentação de 24 bits não está implementada. O modo de vídeo é NTSC 640×448 entrelaçado, fixo. GPUSTAT mantém bits de prontidão forçados e, no modo não entrelaçado, alterna o bit de linha a cada leitura (`gpu.c:456` na cópia). O GP0(1F), de IRQ da GPU, não aparece implementado no switch em `gpu.c:329`. São pontos de compatibilidade a validar conforme os comandos realmente emitidos.

**Proposta:** preservar o renderizador software como referência; capturar pacotes e hashes de VRAM; desenvolver backend GS por etapas, começando por sprites/UI e polígonos comuns. Manter ordering table, máscara, CLUT, mistura e operações de VRAM observáveis pelo jogo. Não trocar tudo por desenho GS sem uma estratégia para leituras de VRAM e texturas que compartilham o mesmo armazenamento.

Medir separadamente lógica, fallback, GTE, rasterização, upload/apresentação, I/O e espera. Usar contagem de quadros da lógica e latência de entrada. O VBlank do runtime é gerado a partir do relógio consultado em `D:\PROJETOS\tekken 3 ps2\ps2\runtime\bios_hle.c:270`, com ressincronização se atrasar; não é prova de sincronização com o retrace real do GS. O contador `g_entregas` conta entradas em exceção de várias fontes, apesar da frase “entregues ao jogo” no log de VBlank. Seu valor acima de `g_vblanks` não prova duplicação de VBlank.

## DualShock, áudio, memória, assets e console real

| Área | Evidência e consequência | Próximo passo |
|---|---|---|
| Entrada física | `D:\PROJETOS\tekken 3 ps2\ps2\runtime\pad.c:5`: botões começam em FFFF e não há produtor que os atualize. Linhas 33–41 só expõem controle digital na porta 1. Não foram encontradas chamadas padInit/padRead no runtime. | Inicializar módulos de controle e libpad, abrir as duas portas, ler/reconectar e atualizar o estado ativo em zero. Primeiro validar digital; depois analógicos e vibração se desejados. Ver [libpad do ps2sdk](https://github.com/ps2dev/ps2sdk/blob/master/ee/rpc/pad/src/libpad.c). |
| Inicialização USB/exFAT | `D:\PROJETOS\tekken 3 ps2\ps2\runtime\main.c:129` inicia memória/vídeo e abre arquivos. Não há bootstrap explícito dos drivers USB/BDM/FatFs. `mass:` depende do ambiente herdado. | Definir contrato de lançamento como aplicativo ELF, carregar ou verificar módulos e aguardar montagem; testar o caminho real via OPL. O fato de OPL ler exFAT não comprova que este ELF tenha o mesmo serviço disponível. Ver [bdmfs_fatfs do ps2sdk](https://github.com/ps2dev/ps2sdk/tree/master/iop/fs/bdmfs_fatfs). |
| SPU/SPU2 | `D:\PROJETOS\tekken 3 ps2\ps2\runtime\io.c:303` termina DMA de SPU sem transferir; não há mixer, síntese ou saída SPU2. | Definir semântica de vozes, RAM de som, ADSR, IRQ e transferências; depois transporte/buffering para IOP/SPU2. Mapear registradores diretamente sem validar temporização não basta. |
| Erro de endereço SPU | `D:\PROJETOS\tekken 3 ps2\ps2\runtime\io.c:368` chama C0C/C0E de ENDX e força zero. Esses offsets correspondem ao volume ADSR e loop start da voz 0; o estado global de canais fica em D9C/D9E. | Corrigir o mapa antes da implementação de áudio; testar round-trip do loop start e comportamento de estado de voz. Ver [mapa de I/O PSX-SPX](https://psx-spx.consoledev.net/ps1/system/iomap/). |
| CDDA/XA | `D:\PROJETOS\tekken 3 ps2\ps2\runtime\cdrom.c:182` apenas marca Play; faixas 2/3 não são abertas. Linhas 312–315 descartam áudio XA. | Implementar leitor de faixas/CUE, progresso da reprodução, decodificação XA e mistura sincronizada. Corrigir F2 antes. |
| FMV/MDEC | `D:\PROJETOS\tekken 3 ps2\ps2\runtime\mdec.c:13` devolve estado constante; `D:\PROJETOS\tekken 3 ps2\ps2\runtime\io.c:254` zera a saída DMA. | Não considerar isso um decoder nem um skip de FMV validado. Implementar decodificação ou um caminho explícito de pular vídeo que preserve eventos e transições do jogo. |
| Memória EE | RAM convidada 2 MiB, VRAM software 1 MiB e imagem de apresentação 640 KiB estão no footprint estático. O ELF soma ~9,12 MiB em seções, mas isso exclui heap, pilhas e reservas adicionais. | Registrar mapa de link, heap livre/mínimo e picos ao carregar overlays e áudio. Incluir a cópia dinâmica do EXE em `D:\PROJETOS\tekken 3 ps2\ps2\runtime\codigo.c:19`. |
| Falha de alocação | `D:\PROJETOS\tekken 3 ps2\ps2\runtime\codigo.c:20` retorna sem erro se malloc falhar; linha 31 considera o trecho original quando não há cópia. | Falhar de modo explícito ou desabilitar execução compilada dependente dessa validação. Uma falha de memória não pode autorizar código obsoleto após overlay. |
| Validação de EXE | `D:\PROJETOS\tekken 3 ps2\ps2\runtime\main.c:93` usa soma unsigned para checar tamanho. Tamanho adulterado pode transbordar antes de fread. A identidade do conteúdo não é verificada em runtime. | Checar `t_size <= PS2_RAM_SIZE - base`, tamanho real, PC/stack e hash da versão esperada. Não foi demonstrado problema com o dump atual. |
| Erros de disco | `D:\PROJETOS\tekken 3 ps2\ps2\runtime\main.c:152` ignora falha ao abrir; `D:\PROJETOS\tekken 3 ps2\ps2\runtime\cdrom.c:307` ignora erro de ler_setor e pode entregar zeros como dados. | Parar a inicialização com mensagem clara se faltar imagem; emitir erro do controlador em falha de setor, sem marcar zeros como leitura bem-sucedida. |
| Assets e persistência | O executável recompilado ainda exige SLUS e disco; TOC/tamanhos são fixos. Memory card é reportado ausente em `D:\PROJETOS\tekken 3 ps2\ps2\runtime\bios_hle.c:331` e 374. | Manifesto local de versão/hashes e extração reproduzível; tratar saves como requisito separado, com integridade e testes de interrupção da gravação. |

## Ordem de execução sugerida para Claude

1. Corrigir F1–F5 com testes de regressão. Acrescentar contadores de GP0, último comando CD, FIFO, estado de evento, fonte de IRQ e PCs recorrentes. Não alterar timers para “fazer andar” sem demonstrar a semântica esperada.
2. Corrigir F6–F8 e os problemas de falha de I/O/alocação. Habilitar avisos no runtime; separar as opções dos fontes gerados quando necessário. Atualizar comentários que ainda dizem que GTE não calcula nada.
3. Obter marcos observáveis: display habilitado e VRAM correta → título → Start → seleção → luta. Guardar captura, log, hash dos fontes e identificação do ELF para cada marco.
4. Integrar controle físico e bootstrap de armazenamento; testar no PS2 real. Usar PCSX2 como ferramenta de desenvolvimento, sem inferir compatibilidade física somente de HostFs.
5. Catalogar/recompilar overlays e medir a luta. Só então priorizar backend GS/VU0 com base no perfil; completar áudio, FMV, saves e cobertura de modos/personagens.

Antes de aplicar qualquer correção, comparar o arquivo e o hash atuais com o manifesto, pois o desenvolvimento continuou durante a revisão. Os testes e evidências estão separados dos fontes de trabalho; **não foi aplicado patch ao projeto**.
