# Fase 2 — Etapa 4: CD-ROM, controle, eventos da BIOS e overlays

## Resultado (14/11/2026, PCSX2 2.9.179 Linux, BIOS scph39001)

O Tekken 3 agora:

1. inicializa o **CD-ROM** e lê o disco (Setloc/ReadN no setor 16 = ISO9660);
2. inicializa GPU (`ResetGraph`), som (SPU + DMA4) e **controle** (porta 1);
3. procura o save `bu00:BASLUS-00402TEKKEN-3`, vê que **não há memory card** e segue;
4. executa **código carregado do disco** (overlays) pelo interpretador:
   ~24 milhões de instruções interpretadas em ~13 s;
5. começa a tocar o **vídeo de abertura** pelo MDEC:
   `MDEC_in_sync timeout` (o MDEC ainda não existe).

## O que foi implementado

| Arquivo | O quê |
|---|---|
| `cdrom.c` | Controlador de CD: Getstat, Setloc, ReadN/ReadS, Pause, Init, Setmode, GetTN/TD, GetlocL/P, SeekL/P, GetID, Test, ReadTOC; INT1/2/3/5 com atraso; DMA3; lê `TEKKEN3.BIN` ou `Tekken 3 (USA) (Track 1).bin` ao lado do ELF (ou `mass:/TEKKEN3/TEKKEN3.BIN`). Áudio CDDA/XA ainda mudo. |
| `pad.c` | Porta SIO0: controle digital na porta 1 (sem botões por enquanto), porta 2 e memory cards ausentes. |
| `bios_hle.c` | Eventos (Open/Close/Enable/Disable/Test/Wait/Deliver/UnDeliverEvent); fila de handlers `SysEnqIntRP`/`SysDeqIntRP`; tratamento padrão dos root counters (ack + evento RCntCNTn, `ChangeClearRCnt`, padrão 1); funções de evento chamadas na exceção; memory card ausente (timeout); `open` falha. |
| `io.c` | IRQs dos timers (alvo/overflow, repetir/one-shot). |
| `interp.c` | Interpretador MIPS R3000A para código sem compilação (overlays). Volta ao código compilado no primeiro salto para uma função conhecida, BIOS ou retorno mágico. |
| `tools/sementes_ponteiros.txt` | 86 funções chamadas só por ponteiro (achadas por varredura) adicionadas às sementes do recompilador — ex.: callback do timer 0x80029784. |

### Como a exceção funciona agora

```
interrupção pendente (IEc+IM2) -> salva registradores
  -> root counters: ack (ChangeClearRCnt) + DeliverEvent(RCntCNTn)
  -> funções de evento (EvMdINTR) do jogo
  -> handlers SysEnqIntRP (prioridade 0..3: func1, se !=0 -> func2)
  -> SetCustomExitFromException do jogo (libetc) -> ReturnFromException
```
As funções do jogo rodam no laço de despacho normal; o retorno delas cai no
endereço mágico 0x00000F00, que continua a sequência.

## Pontos de atenção

- **Desempenho do interpretador**: ~1,8 milhão de instruções/s no PCSX2 — o
  PS1 executa ~33 milhões/s. Os overlays mais usados vão precisar ser
  **pré-compilados** (capturar o código carregado, recompilar e embutir no
  ELF), senão as lutas ficam lentas.
- `dirty_ram_text_native_ok_ranges_from` ainda aceita sempre: se um overlay
  sobrescrever código que foi compilado, o código antigo roda. Precisa de
  detecção de escrita em área de código.
- `VSync: timeout` aparece às vezes: o contador de VBlank da libetc não
  avança enquanto o jogo fica muito tempo com interrupções desligadas
  (provavelmente dentro do código interpretado lento).

## Próximos passos

1. **MDEC** (decodificador de vídeo) — ou pular o vídeo de abertura.
2. **GPU → GS** (Fase 3): primeira imagem na tela.
3. Pré-compilação dos overlays (desempenho).
