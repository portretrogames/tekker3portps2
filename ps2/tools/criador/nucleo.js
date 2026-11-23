/* Criador Tekken 3 PS2 — núcleo: reconhece a imagem do disco do usuário,
 * confere se é o Tekken 3 americano (SLUS-00402), separa as faixas que o
 * port usa e monta a lista de arquivos de APPS/TEKKEN3 (ou um ZIP).
 * Roda no navegador (dentro do Criar_Tekken3_PS2.html) e no Node (testes).
 * Nada sai do computador. Os arquivos grandes nunca são copiados inteiros
 * para a memória: tudo é feito com fatias (Blob.slice) do arquivo escolhido. */
const NucleoT3 = (() => {
  'use strict';

  /* Faixas do disco como o port espera (runtime/cdrom.h). A faixa 2 começa
     com a pausa de 2 s (150 setores, INDEX 00), como na imagem redump. */
  const SETOR = 2352;
  const T1 = 268934, T2 = 11778, T3 = 11923;
  const T1_BYTES = T1 * SETOR, T2_BYTES = T2 * SETOR, T3_BYTES = T3 * SETOR;
  const TUDO_BYTES = T1_BYTES + T2_BYTES + T3_BYTES;   /* imagem num .bin só */
  /* CRC32 da imagem testada no console (redump). Diferente não impede:
     só avisa que aquela cópia não foi testada. */
  const T1_CRC = 0x8131AF42, T2_CRC = 0x28D8F221;
  const EXE = 'SLUS_004.02';
  const PASTA = 'APPS/TEKKEN3/';
  const NOME_F1 = 'TEKKEN3.BIN', NOME_F2 = 'TEKKEN3_2.BIN';   /* nomes que o port procura primeiro */
  const TITLE_CFG = 'title=Tekken 3 (port nativo)\nboot=tekken3.elf\n';   /* igual a ps2/pacote/title.cfg */
  const dec = new TextDecoder(), enc = new TextEncoder();

  class ErroDisco extends Error {
    constructor(codigo, detalhe) { super(codigo); this.codigo = codigo; Object.assign(this, detalhe || {}); }
  }

  /* ---------------------------------------------------------------- CRC32 */
  const tabela = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();
  /* c: valor parcial (comece com 0); devolve o CRC32 do que já passou. */
  function crc32(dados, c) {
    c = ((c || 0) ^ 0xFFFFFFFF) >>> 0;
    for (let i = 0; i < dados.length; i++) c = tabela[(c ^ dados[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }
  const hex8 = (n) => (n >>> 0).toString(16).toUpperCase().padStart(8, '0');

  /* ------------------------------------------------------------- pedaços
     Um pedaço é {arq, ini, fim}: uma faixa dentro de um arquivo escolhido. */
  const pedaco = (arq, ini, fim) => ({ arq, ini, fim, tamanho: fim - ini, nome: arq.name });
  const blobDe = (p) => p.arq.slice(p.ini, p.fim);
  async function ler(p, desloc, n) {
    const ini = p.ini + desloc;
    return new Uint8Array(await p.arq.slice(ini, Math.min(ini + n, p.fim)).arrayBuffer());
  }

  /* ----------------------------------------------- reconhecer os arquivos
     arquivos: lista de File (ou {name, size, slice}). Reconhece pelo tamanho:
     faixa 1, faixa 2, faixa 3 (não usada) ou a imagem inteira num .bin só. */
  function classificar(arquivos) {
    const c1 = [], c2 = [];
    const ignorados = [], desconhecidos = [];
    for (const a of arquivos) {
      const nome = a.name || '';
      if (/\.cue$/i.test(nome)) { ignorados.push({ nome, motivo: 'cue' }); continue; }
      if (/\.(iso|img|mdf|ecm|chd|7z|zip|rar)$/i.test(nome)) { desconhecidos.push({ nome, tamanho: a.size, motivo: 'formato' }); continue; }
      if (a.size === T1_BYTES) c1.push(pedaco(a, 0, T1_BYTES));
      else if (a.size === T2_BYTES) c2.push(pedaco(a, 0, T2_BYTES));
      else if (a.size === T3_BYTES) ignorados.push({ nome, motivo: 'faixa3' });
      else if (a.size === TUDO_BYTES) {
        c1.push(pedaco(a, 0, T1_BYTES));
        c2.push(pedaco(a, T1_BYTES, T1_BYTES + T2_BYTES));
      } else desconhecidos.push({ nome, tamanho: a.size, motivo: 'tamanho' });
    }
    /* Duas faixas 1 (ou 2) = arquivos de mais de uma imagem: não dá para saber
       qual usar sem arriscar misturar discos. */
    if (c1.length > 1 || c2.length > 1)
      throw new ErroDisco('VARIAS_IMAGENS', { nomes: [...new Set([...c1, ...c2].map((p) => p.nome))] });
    if (!c1.length) throw new ErroDisco('SEM_FAIXA1', { desconhecidos });
    return { f1: c1[0], f2: c2[0] || null, ignorados, desconhecidos };
  }

  /* ----------------------------------------- conferir o jogo (SYSTEM.CNF)
     Lê o ISO9660 da faixa 1 (setores de 2352 bytes, modo 2: os 2048 bytes de
     dados começam no byte 24) e procura o SYSTEM.CNF na raiz. */
  const u32 = (d, i) => (d[i] | (d[i + 1] << 8) | (d[i + 2] << 16) | (d[i + 3] << 24)) >>> 0;
  const dadosSetor = async (p, lba, n) => {
    const bruto = await ler(p, lba * SETOR, (n || 1) * SETOR);
    const s = new Uint8Array((n || 1) * 2048);
    for (let k = 0; k < (n || 1); k++) s.set(bruto.subarray(k * SETOR + 24, k * SETOR + 24 + 2048), k * 2048);
    return s;
  };
  async function conferirJogo(f1) {
    const pvd = await dadosSetor(f1, 16);
    const id = dec.decode(pvd.subarray(1, 6));
    if (pvd[0] !== 1 || id !== 'CD001') throw new ErroDisco('NAO_E_DISCO');
    const raizLba = u32(pvd, 156 + 2), raizTam = u32(pvd, 156 + 10);
    const raiz = await dadosSetor(f1, raizLba, Math.min(8, Math.ceil(raizTam / 2048)));
    let cnf = null;
    for (let o = 0; o < raiz.length;) {
      const len = raiz[o];
      if (!len) { o = (Math.floor(o / 2048) + 1) * 2048; continue; }
      const nlen = raiz[o + 32];
      const nome = dec.decode(raiz.subarray(o + 33, o + 33 + nlen)).toUpperCase();
      if (nome === 'SYSTEM.CNF;1' || nome === 'SYSTEM.CNF') { cnf = { lba: u32(raiz, o + 2), tam: u32(raiz, o + 10) }; break; }
      o += len;
    }
    if (!cnf) throw new ErroDisco('NAO_E_PS1');
    const texto = dec.decode((await dadosSetor(f1, cnf.lba)).subarray(0, Math.min(cnf.tam, 2048)));
    /* BOOT = cdrom:\TEKKEN3\SLUS_004.02;1 — vale o nome do arquivo, a pasta não */
    const boot = (texto.match(/BOOT\s*=\s*cdrom:\\?([^;\s]+)/i) || [])[1] || '';
    if (boot.split('\\').pop().toUpperCase() !== EXE) throw new ErroDisco('OUTRO_JOGO', { boot: boot || '?' });
    return { boot };
  }

  /* CRC32 de um pedaço, em fatias de 8 MB; progresso(feito, total). */
  async function crcPedaco(p, progresso) {
    const PASSO = 8 << 20;
    let c = 0;
    for (let i = 0; i < p.tamanho; i += PASSO) {
      c = crc32(await ler(p, i, PASSO), c);
      if (progresso) progresso(Math.min(i + PASSO, p.tamanho), p.tamanho);
    }
    return c;
  }

  /* Tudo de uma vez: reconhece, confere o jogo e calcula os CRCs.
     progresso(texto, fração 0..1). */
  async function conferir(arquivos, progresso) {
    const c = classificar(arquivos);
    const jogo = await conferirJogo(c.f1);
    const total = c.f1.tamanho + (c.f2 ? c.f2.tamanho : 0);
    const crc1 = await crcPedaco(c.f1, (f) => progresso && progresso('Conferindo a faixa 1', f / total));
    const crc2 = c.f2 ? await crcPedaco(c.f2, (f) => progresso && progresso('Conferindo a faixa 2', (c.f1.tamanho + f) / total)) : null;
    return {
      ...c, jogo,
      crc1, crc1Ok: crc1 === T1_CRC,
      crc2, crc2Ok: c.f2 ? crc2 === T2_CRC : null,
    };
  }

  /* --------------------------------------------------- o que vai gravar
     Devolve [{caminho, dados (Uint8Array) | pedaco}], relativo à raiz do pendrive.
     As faixas vêm primeiro: se o pendrive encher no meio, o ELF e o title.cfg
     de uma instalação anterior continuam lá, inteiros (o navegador só troca um
     arquivo quando ele termina de ser gravado). */
  const texto = (s) => enc.encode(s);
  function arquivosParaGravar({ elf, leiaMe, conferido }) {
    const lista = [{ caminho: PASTA + NOME_F1, pedaco: conferido.f1, crc: conferido.crc1 }];
    if (conferido.f2) lista.push({ caminho: PASTA + NOME_F2, pedaco: conferido.f2, crc: conferido.crc2 });
    lista.push(
      { caminho: PASTA + 'tekken3.elf', dados: elf },
      { caminho: PASTA + 'title.cfg', dados: texto(TITLE_CFG) },
      { caminho: PASTA + 'LEIA-ME.txt', dados: texto(leiaMe) },
    );
    return lista;
  }

  /* ----------------------------------------------------------------- ZIP
     ZIP sem compressão, montado como um Blob feito de fatias: os 600 MB da
     faixa 1 não são copiados para a memória. Todos os arquivos ficam abaixo
     de 4 GB e o ZIP inteiro também, então não precisa de ZIP64. */
  function gerarZipBlob(lista, quando) {
    const d = quando || new Date();
    const hora = (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1);
    const dia = ((d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
    const partes = [], central = [];
    let pos = 0;
    for (const a of lista) {
      const nome = texto(a.caminho);
      const n = a.dados ? a.dados.length : a.pedaco.tamanho;
      const crc = a.dados ? crc32(a.dados) : a.crc;
      if (crc === undefined || crc === null) throw new Error('falta o CRC de ' + a.caminho);
      const l = new Uint8Array(30 + nome.length), dl = new DataView(l.buffer);
      dl.setUint32(0, 0x04034B50, true); dl.setUint16(4, 20, true); dl.setUint16(6, 0x0800, true);
      dl.setUint16(10, hora, true); dl.setUint16(12, dia, true); dl.setUint32(14, crc, true);
      dl.setUint32(18, n, true); dl.setUint32(22, n, true); dl.setUint16(26, nome.length, true);
      l.set(nome, 30);
      const c = new Uint8Array(46 + nome.length), dc = new DataView(c.buffer);
      dc.setUint32(0, 0x02014B50, true); dc.setUint16(4, 20, true); dc.setUint16(6, 20, true);
      dc.setUint16(8, 0x0800, true); dc.setUint16(12, hora, true); dc.setUint16(14, dia, true);
      dc.setUint32(16, crc, true); dc.setUint32(20, n, true); dc.setUint32(24, n, true);
      dc.setUint16(28, nome.length, true); dc.setUint32(42, pos, true);
      c.set(nome, 46);
      partes.push(l, a.dados ? a.dados : blobDe(a.pedaco));
      central.push(c);
      pos += l.length + n;
    }
    const tamCentral = central.reduce((s, c) => s + c.length, 0);
    const fim = new Uint8Array(22), df = new DataView(fim.buffer);
    df.setUint32(0, 0x06054B50, true); df.setUint16(8, lista.length, true); df.setUint16(10, lista.length, true);
    df.setUint32(12, tamCentral, true); df.setUint32(16, pos, true);
    return new Blob([...partes, ...central, fim], { type: 'application/zip' });
  }

  return { SETOR, T1, T2, T3, T1_BYTES, T2_BYTES, T3_BYTES, TUDO_BYTES, T1_CRC, T2_CRC, EXE, PASTA,
           NOME_F1, NOME_F2, TITLE_CFG, ErroDisco, crc32, hex8, classificar, conferirJogo, crcPedaco,
           conferir, arquivosParaGravar, gerarZipBlob, blobDe };
})();

if (typeof module !== 'undefined') module.exports = NucleoT3;
