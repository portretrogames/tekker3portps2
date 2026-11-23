# Fase 2 — etapa 1: linkagem

Os 45 arquivos C gerados e o dispatch foram compilados para o EE e ligados
com o ps2sdk. A build usa `-O2 -G0`, `PSX_NO_DEBUG_TOOLS=1` e
`PSX_NATIVE_BUILD=1`, sem `PSX_ENABLE_BLOCK_CYCLES`.

- ELF de referência da implementação inicial: **7.797.336 bytes**;
  `text`: 5.378.828 bytes;
  BSS: 2.122.880 bytes. O tamanho depende da toolchain.
- RAM de 2 MiB alinhada, com espelhos nos primeiros 8 MiB físicos.
- Leituras e escritas fora da RAM apenas registram o acesso; não há hardware.
- `__muldi3` é fornecido pela libgcc, não substituído por stub. Os demais
  símbolos da lista são variáveis, funções de memória ou stubs no runtime.
- O dispatch gerado chama uma verificação de validade de código. O stub a
  aceita provisoriamente, sem detectar código modificado ou overlays.
- `main` inicia o estado e chama o dispatch compilado em `0x80079C70`;
  não carrega o executável ou seus dados na RAM nem fornece OpenBIOS.
  Por isso o ELF **não é jogável** e o sucesso da linkagem não demonstra
  inicialização, tela de título, imagem ou FPS.
- Nesta primeira versão, somente as leituras half/word geradas usavam caminho
  inline para RAM; as outras operações foram corrigidas na etapa 2.
