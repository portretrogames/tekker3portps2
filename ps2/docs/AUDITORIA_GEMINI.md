# Resposta ao briefing de auditoria do Gemini (15/11/2026, V19)

Correções de fatos do briefing: são **45** shards (`full_00`…`full_44`), não 16;
o pós-processador é `ps2/tools/transformar_gerado.py` (não existe
`patch_recomp.py`); não existem `ps2_gpu.c` nem `timers.c` (GPU→GS está em
`gpu.c` + `gs_render.c`; timers em `io.c`); não há SPU emulado (o jogo está mudo).

## Ponto 1 — SWL/SWR (confirmado, corrigido na V19)
`psx_swl`/`psx_swr` em `SLUS_004.02_decls.h` liam com `cpu->read_word()`
(ponteiro de função → `jalr`): 85 + 85 locais no código gerado. Agora
`fast_memory.h` transforma `cpu->read_word/read_half/read_byte(x)` em leitura
rápida inline (como já era feito com `write_*`). Shard 06: 12 `jalr` → 0.

## Ponto 2 — ganchos `psx_ws_*` (confirmado, corrigido na V19)
44 chamadas no código do Tekken (`psx_ws_background_cull_bltz/margin`,
`cull_sltiu`, `background_packet_tag`, `full_angle_widen`), cada uma um `jal`
para `stubs.c`. Agora são `static inline` em `fast_memory.h` com o mesmo
comportamento 4:3 (shard 06: 0 chamadas). `psx_ws_mmx6_*` e
`psx_datashard_*` são só declarados, não chamados.
`g_debug_last_store_pc` já era eliminado desde a V15.

## Ponto 3 — `gerado/` × `build/r/` (já correto)
Fluxo: `gerado/X.c` → `tools/transformar_gerado.py` → `build/r/X.c` → `build/X.o`
(regra no `ps2/Makefile`). `build*/` e `gerado/` estão no `.gitignore`;
`build/r/` é descartável e nunca é editado à mão.

## Checklist
1. **Memória.** Alinhamento: o PS1 também gera exceção em acesso
   desalinhado, então o jogo só faz acessos alinhados; mesmo assim
   `ram.h` mascara (`& ~3`, `& ~1`), e não há exceção no R5900.
   **Scratchpad:** estava no caminho lento — **na V19 entrou no caminho rápido
   inline** (KUSEG e KSEG0, igual a `memory.c`). MMIO `0x1F801000–0x1F801FFF`:
   `memory.c` → `io.c` (`ps2_io_read/write`, switch por registrador).
2. **GTE (`gte.c`).** Nenhum `double`/`float`: tudo inteiro de 32/64 bits.
   Não usa VU0. Custa 10–17% do EE na luta (candidato a otimização).
3. **GPU→GS.** Os pacotes GIF são escritos pelo segmento **UCAB (0x30000000)**
   — sem cache —, então não precisam de `FlushCache` antes do DMA; a fila é
   alinhada a 16/64 bytes; texturas da VRAM do PS1 são copiadas para a fila.
   CLUT 4/8 bpp: o GS lê os índices da VRAM do PS1 em páginas de cache
   (`PSMT4/PSMT8`) e a paleta com `CSM2`; não há decodificação por pixel na CPU.
4. **Despacho.** Busca binária na tabela de 22.325 funções com **cache direto
   PC→entrada** (8192 posições, 99% de acerto, V17). O custo maior é o modelo
   CPS: cada `jal`/`jr $ra` volta ao laço de `main.c` (~275 mil/s na luta).
5. **CD / IRQ / laço.** A checagem de IRQ roda a cada 256 desvios do código
   gerado (`ps2_irq_poll`): CD, timers, VBlank (relógio derivado do COUNT do
   EE), controle. As respostas do CD (INT3/INT2/INT1) são agendadas com
   atraso; o drive espera o jogo pedir o setor anterior; leitura do `.bin`
   assíncrona. Funciona em lutas de 12+ min no console.

## Riscos de crash já tratados
Listas do DMA2 em ciclo (V12), parser ISO sem limites (V18), envio que
cruza a linha 511 da VRAM (V18), leitura assíncrona × gravação do log (V11/V13).

## Custo da V19
Inline de scratchpad/SWL/SWR aumentou o texto gerado de 8,4 MB para 11 MB
(I-cache de 16 KB do EE). Precisa de medição no console.

---

# Auditoria V19 do Gemini — o que foi feito (V20)

| Item | Veredito | O que mudou |
|---|---|---|
| 1.1 func1 chama DeliverEvent | **Bug real** | `bios_hle.c` guarda etapa e v0 antes dos callbacks e retoma depois. Teste novo em `revisao_bios_test.c` (callback roda, func2 roda 1 vez com v0 = 5, func1 não repete). |
| 1.2 IRQ falsa ao escrever contador/alvo | **Bug real** | `io.c`: `timer_marcas()` + `timer_sincronizar()` nas escritas de COUNT e alvo. Teste novo em `revisao_test.c`. |
| 1.3 INT1 antigo no ReadN/ReadS | Procede | `comecar_leitura()` chama `remover_int1()`. |
| 1.4 underflow no DMA6 | **Não procede** | `a - sig.madr < n*4+4` em unsigned é o teste de intervalo `sig.madr ∈ (a-n*4-4, a]`; o "wrap" dá um número grande, que corretamente não invalida (a lista está fora da OT limpa). Invalidar em todo DMA6 desligaria o pulo de redesenho sempre que o jogo limpa a OT do *outro* buffer. Mantido. |
| 1.5 ISO / MDEC / descritor | Procede | `dir_tam` limitado a 256 setores; MDEC compacta a sobra antes de decodificar; `close(fd)` quando não há descritor do IOP. |
| 2.1 `preencher()` na sombra | Procede, **com ajuste** | A sombra em RAM ainda é usada para montar texturas de 4/8 bits (`pagina_indices` em `gs_render.c`), então pular sempre quebraria texturas. Agora o GS marca os blocos 64×256 já usados como textura e o preenchimento só vai para a sombra se tocar um deles. **Não** sobe `g_ps2_vram_geracao`: o preenchimento dentro da lista já entra no hash do redesenho, e subir a geração desligaria o pulo em todo quadro. |
| 2.2 painel com 600 GIFtags | Procede | Pontos em lotes de até 128 por GIFtag. |
| 2.3 varredura no diagnóstico | Procede (ganho pequeno: roda a cada 2 s) | Pulada com o GS ativo. |
| 2.4 CLUT em textura de 15 bits | Procede | `mesma_chave` ignora a CLUT nesse caso. |
| 2.5 laço do DMA6 | Procede | Escrita direta na RAM e marcação por grânulo de 64 bytes. |
| 3.1 `scratchpad_hit` duas vezes | Procede | Uma chamada. |
| 3.2 ciclos da GPU somados 2 vezes | Procede | O GPU% do painel/log pode cair um pouco (antes contava a mais). |
| 3.3 macros sem parênteses | Procede (sem caso atual quebrado) | `transformar_gerado.py` troca `cpu->read_*/write_*(` por `ps2_fast_*(` nos shards e numa cópia de `SLUS_004.02_decls.h` em `build/r/`; as macros saíram de `fast_memory.h`. Código gerado idêntico (11,04 MB). |

Verificado: `make test` (com os 2 testes novos), ELF sem avisos, PCSX2 em luta.
Falta: medir no console.
