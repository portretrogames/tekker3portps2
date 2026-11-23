/* Testes do núcleo do criador (node tools/criador/testar_nucleo.js [pasta_do_disco]).
 * Com a pasta do disco (as faixas redump), confere o jogo de verdade; sem
 * ela, só os testes sintéticos. */
'use strict';
const fs = require('fs'), path = require('path'), os = require('os'), { execFileSync } = require('child_process');
const N = require('./nucleo.js');
let falhas = 0;
const ok = (cond, msg) => { if (!cond) { falhas++; console.log('FALHOU: ' + msg); } };
const comNome = (blob, name) => ({ name, size: blob.size, slice: (a, b) => blob.slice(a, b) });

/* Disco sintético no formato da faixa 1 (setores de 2352, dados no byte 24):
   PVD no setor 16, raiz no 18 e, se 'cnf' for dado, o SYSTEM.CNF no 19. */
function discoSintetico(cnf) {
  const S = 2352, setores = 24, d = new Uint8Array(setores * S);
  const dados = (lba) => d.subarray(lba * S + 24, lba * S + 24 + 2048);
  const u32 = (b, o, v) => { b[o] = v & 255; b[o + 1] = (v >> 8) & 255; b[o + 2] = (v >> 16) & 255; b[o + 3] = (v >>> 24) & 255; };
  const pvd = dados(16);
  pvd[0] = 1; pvd.set(Buffer.from('CD001'), 1); pvd[6] = 1;
  u32(pvd, 156 + 2, 18); u32(pvd, 156 + 10, 2048);
  const raiz = dados(18);
  let o = 0;
  const reg = (nome, lba, tam) => {
    const n = Buffer.from(nome), len = 33 + n.length + (n.length % 2 ? 0 : 1);
    raiz[o] = len; u32(raiz, o + 2, lba); u32(raiz, o + 10, tam); raiz[o + 32] = n.length; raiz.set(n, o + 33); o += len;
  };
  reg('\0', 18, 2048); reg('\x01', 18, 2048);
  reg('LEIAME.TXT;1', 20, 10);
  if (cnf) { const t = Buffer.from(cnf); reg('SYSTEM.CNF;1', 19, t.length); dados(19).set(t); }
  const blob = new Blob([d]);
  return { arq: blob, ini: 0, fim: d.length, tamanho: d.length, nome: 'sintetico.bin' };
}
(async () => {
  // 1. reconhecimento pelo tamanho
  const fake = (name, size) => ({ name, size, slice: () => new Blob([]) });
  let c = N.classificar([fake('a.cue', 300), fake('t1.bin', N.T1_BYTES), fake('t2.bin', N.T2_BYTES), fake('t3.bin', N.T3_BYTES)]);
  ok(c.f1 && c.f2 && c.f1.tamanho === N.T1_BYTES && c.f2.tamanho === N.T2_BYTES, 'faixas separadas');
  ok(c.ignorados.length === 2, 'cue e faixa 3 ignorados');
  c = N.classificar([fake('tudo.bin', N.TUDO_BYTES)]);
  ok(c.f1.ini === 0 && c.f1.fim === N.T1_BYTES && c.f2.ini === N.T1_BYTES && c.f2.fim === N.T1_BYTES + N.T2_BYTES, 'um .bin só');
  try { N.classificar([fake('jogo.iso', 700e6)]); ok(false, 'ISO devia falhar'); }
  catch (e) { ok(e.codigo === 'SEM_FAIXA1' && e.desconhecidos[0].motivo === 'formato', 'ISO recusada'); }
  try { N.classificar([fake('outro.bin', 123456)]); ok(false, 'tamanho errado devia falhar'); }
  catch (e) { ok(e.codigo === 'SEM_FAIXA1' && e.desconhecidos[0].motivo === 'tamanho', 'tamanho errado recusado'); }

  try { N.classificar([fake('a.bin', N.T1_BYTES), fake('tudo.bin', N.TUDO_BYTES)]); ok(false, 'duas imagens devia falhar'); }
  catch (e) { ok(e.codigo === 'VARIAS_IMAGENS' && e.nomes.length === 2, 'duas imagens recusadas'); }

  // 1b. SYSTEM.CNF num disco sintético
  let r0 = await N.conferirJogo(discoSintetico('BOOT = cdrom:\\TEKKEN3\\SLUS_004.02;1\r\nTCB = 4\r\n'));
  ok(/SLUS_004\.02$/.test(r0.boot), 'sintético válido');
  try { await N.conferirJogo(discoSintetico('BOOT = cdrom:\\SLUS_001.23;1\r\n')); ok(false, 'outro jogo devia falhar'); }
  catch (e) { ok(e.codigo === 'OUTRO_JOGO' && e.boot === 'SLUS_001.23', 'outro jogo recusado (' + e.codigo + ')'); }
  try { await N.conferirJogo(discoSintetico(null)); ok(false, 'sem SYSTEM.CNF devia falhar'); }
  catch (e) { ok(e.codigo === 'NAO_E_PS1', 'sem SYSTEM.CNF recusado (' + e.codigo + ')'); }

  // 2. faixa 1 do tamanho certo mas sem disco dentro
  const vazio = { name: 'zeros.bin', size: N.T1_BYTES, slice: (a, b) => new Blob([new Uint8Array(b - a)]) };
  try { await N.conferir([vazio]); ok(false, 'zeros devia falhar'); }
  catch (e) { ok(e.codigo === 'NAO_E_DISCO', 'zeros recusados (' + e.codigo + ')'); }

  // 3. CRC incremental = CRC inteiro
  const amostra = new Uint8Array(100000).map((_, i) => (i * 7) & 255);
  ok(N.crc32(amostra) === N.crc32(amostra.subarray(60000), N.crc32(amostra.subarray(0, 60000))), 'CRC em partes');

  // 4. ZIP pequeno, conferido pelo unzip
  const elf = new Uint8Array([0x7F, 0x45, 0x4C, 0x46, 1, 2, 3]);
  const dadosF1 = new Uint8Array(5000).map((_, i) => i & 255);
  const f1 = { arq: new Blob([dadosF1]), ini: 1000, fim: 4000, tamanho: 3000, nome: 'x' };
  const lista = N.arquivosParaGravar({ elf, leiaMe: 'oi', conferido: { f1, crc1: N.crc32(dadosF1.subarray(1000, 4000)), f2: null } });
  ok(lista.map((a) => a.caminho).join(',') === 'APPS/TEKKEN3/TEKKEN3.BIN,APPS/TEKKEN3/tekken3.elf,APPS/TEKKEN3/title.cfg,APPS/TEKKEN3/LEIA-ME.txt', 'lista de arquivos (faixas primeiro)');
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'criador_t3_'));
  try {
    const zipArq = path.join(tmp, 'teste.zip');
    fs.writeFileSync(zipArq, Buffer.from(await N.gerarZipBlob(lista).arrayBuffer()));
    try { execFileSync('unzip', ['-tq', zipArq]); } catch (e) { ok(false, 'unzip -t: ' + e.stdout); }
    const dentro = execFileSync('unzip', ['-p', zipArq, 'APPS/TEKKEN3/TEKKEN3.BIN']);
    ok(Buffer.compare(dentro, Buffer.from(dadosF1.subarray(1000, 4000))) === 0, 'fatia certa dentro do ZIP');
    ok(execFileSync('unzip', ['-p', zipArq, 'APPS/TEKKEN3/title.cfg']).toString() === N.TITLE_CFG, 'title.cfg');
  } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
  ok(fs.readFileSync(path.join(__dirname, '../../pacote/title.cfg'), 'utf8') === N.TITLE_CFG, 'title.cfg igual ao do pacote');

  // 5. o disco de verdade (opcional)
  const pasta = process.argv[2];
  const nomeFaixa = (n) => path.join(pasta || '', `Tekken 3 (USA) (Track ${n}).bin`);
  if (pasta && fs.existsSync(nomeFaixa(1)) && typeof fs.openAsBlob === 'function') {
    const tem = [1, 2, 3].filter((n) => fs.existsSync(nomeFaixa(n)));
    const b = {};
    for (const n of tem) b[n] = await fs.openAsBlob(nomeFaixa(n));
    let r = await N.conferir(tem.map((n) => comNome(b[n], path.basename(nomeFaixa(n)))));
    ok(/SLUS_004\.02$/i.test(r.jogo.boot) && r.crc1Ok && (!b[2] || r.crc2Ok), 'disco em faixas: ' + N.hex8(r.crc1) + ' ' + (r.crc2 === null ? '-' : N.hex8(r.crc2)));
    if (b[2] && b[3]) {
      r = await N.conferir([comNome(new Blob([b[1], b[2], b[3]]), 'Tekken 3 (USA).bin')]);
      ok(r.crc1Ok && r.crc2Ok, 'disco num .bin só');
    }
    r = await N.conferir([comNome(b[1], 'so_a_faixa1.bin')]);
    ok(r.crc1Ok && r.f2 === null, 'só a faixa 1');
    console.log('disco de verdade conferido (faixas ' + tem.join(', ') + ')');
  } else console.log('(sem a pasta do disco ou sem fs.openAsBlob: testes do disco de verdade pulados)');

  console.log(falhas ? `criador: ${falhas} falha(s)` : 'criador: testes OK');
  process.exit(falhas ? 1 : 0);
})().catch((e) => { console.log('ERRO', e); process.exit(1); });
