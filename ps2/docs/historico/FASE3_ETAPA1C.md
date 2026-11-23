# Fase 3, etapa 1c — GTE (parcial)

## Situação

Foram implementados os acessos aos registradores de dados e controle,
incluindo FIFO SXY, conversão IRGB/ORGB, LZCS/LZCR, extensão de sinal e
escrita do FLAG. O estado permanece nos arrays de `CPUState`, compartilhados
pelo código gerado e pelo interpretador. Testes cobrem esses acessos.

**O GTE ainda não está completo:** `gte_execute` continua sendo um stub.
Nenhum dos comandos matemáticos, incluindo RTPS, NCLIP e AVSZ3, foi
implementado ou validado. O gancho `gte_precision_store_word` também continua
em stub; a escrita normal já é feita pelo código gerado antes desse gancho.
Os testes existentes não demonstram correção dos comandos nem da geometria.

Foram consultados PSXRecomp (mstan, PolyForm Noncommercial 1.0.0),
Tekken3Recompiled (FishB0nes98) e PSX-SPX. Uma conversão direta da
referência não garante fidelidade em casos como a matriz reservada e FC do
MVMVA; é necessário validar comandos, saturações, overflow e divisão UNR.

## PCSX2 Linux — versão parcial

ELF compilado no Ubuntu WSL e executado por 90 segundos no PCSX2 2.9.179,
com HostFs e WSLg, usando o procedimento manual do guia Linux. O script
automático depende de Xvfb, que não estava funcional neste ambiente. O log
completo ficou nos arquivos da sessão como `pcsx2-gte-parcial.log`.

- `MDEC_in_sync timeout` e `PARADA:` não apareceram no log.
- A partir de aproximadamente 49 segundos apareceram `stub: gte_execute`
  e `stub: gte_precision_store_word`; os comandos GTE ainda não calculam.
- Após acessos MDEC e DMA, houve leitura e escrita de `1f8010f6` identificadas
  como `?`. A BIOS registrou, entre outras chamadas, `open` do memory card
  ausente e `_card_load`.
- O último registro periódico antes do encerramento mostrou `pc=8006d760`,
  25.165.824 despachos e 19.315.980 instruções interpretadas; isso não é
  uma parada do jogo.

O teste anterior parou em `800b74b0`, mas antecedeu a mudança de overlays no
commit `7566af2`. Portanto não é possível atribuir a diferença aos acessos
de registradores do GTE. A execução parcial não valida a cena 3D.

## Conclusão (Claude, 14/11/2026)

Comandos implementados em `runtime/gte.c` (em cima do acesso a registradores
do Copilot), seguindo o psx-spx:

- RTPS/RTPT (com divisão UNR H/SZ3, saturação de SX/SY/SZ, depth cue IR0 e
  o quirk da flag do IR3 calculada sobre MAC3>>12), NCLIP, OP, SQR,
  AVSZ3/AVSZ4, MVMVA (incluindo a matriz "reservada" e o bug do vetor FC),
  NCS/NCT, NCCS/NCCT, NCDS/NCDT, CC, CDP, DCPL, DPCS/DPCT, INTPL, GPF, GPL.
- Overflow de 44 bits verificado a cada soma (MAC1-3), 32 bits no MAC0, todas
  as flags e o bit 31 de erro do FLAG.
- `gte_precision_store_word` não faz nada no PS2 (gancho de precisão do PC).

Testes (`make test`): NCLIP, AVSZ3, RTPS com matriz identidade (SX/SY/SZ e
sem erro no FLAG) e SQR.

PCSX2 Linux, 90 s: sem `PARADA`, sem avisos de stub do GTE; o jogo segue
alternando código compilado e overlays interpretados (~24 milhões de
instruções interpretadas). Sem GPU ainda não dá para ver a cena.

Otimização com VU0: depois que a GPU mostrar imagem.
