# Resposta à revisão técnica (ChatGPT/Codex, 14/11/2026)

Revisão original: [REVISAO_TECNICA_CHATGPT.md](REVISAO_TECNICA_CHATGPT.md).
Todos os 8 achados foram reproduzidos no código atual antes de corrigir, com
os testes de reprodução da revisão, e viraram testes de regressão no
`make test`.

| Achado | Correção | Teste |
|---|---|---|
| F1 — GPU_cw da BIOS descartado | `bios_hle.c`: A0(48h) SendGP1, A0(49h) GPU_cw, A0(4Ah) GPU_cwp, A0(4Bh) lista encadeada, A0(4Ch) abort | `revisao_test.c` |
| F2 — setor XA travava a leitura | `cdrom.c`: setor XA libera o próximo setor sem exigir pedido de dados | `revisao_cd_test.c` |
| F3 — callback fora de exceção perdido | `bios_hle.c`: a fila não é mais zerada na exceção; `DeliverEvent` fora de exceção roda os callbacks na hora e volta com v0=1 | `revisao_bios_test.c` |
| F4 — WaitEvent retornava cedo | `bios_hle.c`: repete a chamada (pc=0xB0) atendendo interrupções até o evento acontecer | `revisao_test.c` |
| F5 — IRQ de alvo sem reset | `io.c`: alvo e overflow contados separadamente; bits 11/12 do modo | `revisao_test.c` |
| F6 — DICR sem IRQ | `io.c`: transição 0→1 do bit mestre na escrita gera IRQ_DMA | `revisao_test.c` |
| F7 — shift de negativo no GTE (UB) | `gte.c`: escalas com multiplicação (x 4096) | `gte_test.c` com UBSan |
| F8 — dependências de header | `Makefile`: `-MMD -MP` + `-include *.d` (verificado: mudar `gpu.h` recompila os usuários) | — |

Outros pontos da revisão também corrigidos:

- ENDX do SPU estava nos offsets da voz 0 (C0C/C0E); agora em D9C/D9E.
- `codigo.c`: sem memória para a cópia do EXE o runtime para (antes,
  autorizaria código compilado antigo depois de overlay).
- `main.c`: checagem de tamanho do EXE sem overflow; sem imagem do disco o
  runtime para com mensagem clara; aviso do `strncat` removido.
- `cdrom.c`: erro de leitura da imagem gera INT5 em vez de entregar zeros.
- README: descrição precisa ("recompilação estática com interpretador como
  reserva para overlays"), em vez de "sem emulador".

Pontos da revisão que continuam como trabalho futuro (já no plano): controle
físico (libpad), bootstrap USB/exFAT no PS2 real, áudio (SPU2, CDDA, XA),
MDEC real, saves, catálogo e pré-compilação de overlays, backend GS em
hardware e perfil de desempenho.

PCSX2 (90 s) depois das correções: mesmo ponto de antes — exibição 368x480
ativa, texturas carregadas, área da tela ainda preta (investigação da tela
preta segue na etapa 2b).
