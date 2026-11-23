# Fase 3 — Etapa 1: MDEC provisório

## Implementação

- `runtime/mdec.c`: comandos e dados de entrada são descartados; leitura de
  dados devolve zero. O status é `0x90000000`: saída vazia (bit 31), entrada
  solicitada (bit 28), sem ocupado (bit 29) nem solicitação de saída (bit 27).
  Uma escrita com bit 31 no controle reinicia o estado provisório.
- `runtime/io.c`: DMA 0 termina imediatamente descartando a entrada; DMA 1
  escreve zeros na RAM e termina imediatamente. O tamanho em palavras usa
  BCR: no modo manual, tamanho do bloco; no modo por requisição, tamanho
  multiplicado pelo número de blocos. Tamanho zero representa 65536 palavras;
  contagem de blocos zero é tratada como um bloco, seguindo a referência
  local. Endereços dão a volta na RAM de 2 MB; o bit de direção do endereço
  é respeitado. As flags de conclusão e IRQ usam a lógica existente do DMA.
- Não há decodificação de vídeo nem saída visual; os zeros são um substituto
  provisório dos dados de imagem.

## Teste no PCSX2

Em 14/11/2026, o ELF recompilado foi iniciado por `ps2/rodar_pcsx2.bat` no
PCSX2 Windows configurado. O processo foi encerrado após mais de 90 segundos; o jogo
já havia parado antes disso. O log anterior registrava repetidos
`MDEC_in_sync timeout`; nesta execução não houve nenhuma ocorrência e o jogo
avançou até outros despachos. Isso não comprova reprodução ou conclusão do
vídeo de abertura.

Sequência observada depois do reset do MDEC:

```text
io: 1a escrita MDEC          1f801824 = 80000000
io: 1a leitura MDEC          1f801824 = 90000000
io: 1a escrita DMA canal     1f801080 = 800ebe28
io: 1a escrita DMA canal     1f801084 = 00010020
io: 1a escrita MDEC          1f801820 = 40000001
io: 1a leitura DMA canal     1f801098 = 00000000
io: 1a leitura ?             1f8010f6 = 00000000
io: 1a escrita ?             1f8010f6 = 00000000
io: 1a escrita DMA canal     1f801090 = 800fc598
io: 1a escrita DMA canal     1f801094 = 00540020
io: 1a leitura TIMER         1f801128 = 0000ffff
vivo: 13631488 despachos, pc=8004ce54, interpretadas=3662640
PARADA: salto sem codigo compilado em 800b74b0 (ra=800526b0 a0=801fff90 a1=00000000 t1=00007f48)
```

Não houve novas linhas `bios:` entre o reset do MDEC e a parada. A última
antes disso foi `A0(ACh) _card_load` (`ra=8004c614`). Também ocorreram
`VSync: timeout`, já observados na fase anterior. O processo reiniciou para
o menu do PS2 após a parada; o motivo do salto sem código ainda exige análise.
O log completo local permanece em `docs/ultimo_log_pcsx2.txt` e não é
versionado.
