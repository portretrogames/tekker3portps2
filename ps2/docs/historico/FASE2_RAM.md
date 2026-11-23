# Fase 2 — Memória RAM do PS1

## Escopo desta etapa

`runtime/ram.c` fornece a RAM de 2 MB e os quatro espelhos que ocupam o
intervalo físico `0x00000000..0x007FFFFF`. Endereços KUSEG, KSEG0 e KSEG1
são convertidos para endereço físico antes do mapeamento. Leituras e escritas
de 8, 16 e 32 bits são feitas direto na memória (PS1 e EE são little-endian).
Como o PS1 exige alinhamento, acessos de 16/32 bits desalinhados são
alinhados para baixo — nunca cruzam a borda de 2 MB.

O módulo também expõe o ponteiro base e a conversão para offset, para o runtime
integrar depois o caminho rápido inline. Endereços fora da RAM retornam
`0` (não mapeado); a camada de memória será responsável por encaminhá-los a
scratchpad, BIOS ou dispositivos de I/O.

## Teste

O teste independente `tests/ram_test.c` cobre espelhamento entre segmentos,
endereços KSEG, ordem little-endian, cruzamento da borda dos 2 MB, rejeição de
endereços fora da RAM e limpeza da memória.

Em Linux/WSL:

```sh
gcc -std=c99 -Wall -Wextra -Werror ps2/runtime/ram.c ps2/tests/ram_test.c -o /tmp/ps2-ram-test
/tmp/ps2-ram-test
```

A interface inicial de memória usa este módulo e implementa parte dos
símbolos do runtime. Os detalhes e as limitações estão em `FASE2_ETAPA2.md`.
