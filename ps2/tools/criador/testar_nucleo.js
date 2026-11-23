/* Testes do núcleo do criador (node tools/criador/testar_nucleo.js [pasta_do_disco]).
 * Com a pasta do disco (as faixas redump), confere o jogo de verdade; sem
 * ela, só os testes sintéticos. */
'use strict';
const fs = require('fs'), path = require('path'), os = require('os'), { execFileSync } = require('child_process');
const N = require('./nucleo.js');
let falhas = 0;
const ok = (cond, msg) => { if (!cond) { falhas++; console.log('FALHOU: ' + msg); } };
const comNome = (blob, name) => ({ name, size: blob.size, slice: (a, b) => blob.slice(a, b) });
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
  ok(lista.map((a) => a.caminho).join(',') === 'APPS/TEKKEN3/tekken3.elf,APPS/TEKKEN3/title.cfg,APPS/TEKKEN3/LEIA-ME.txt,APPS/TEKKEN3/TEKKEN3.BIN', 'lista de arquivos');
  const zipArq = path.join(os.tmpdir(), 'criador_t3_teste.zip');
  fs.writeFileSync(zipArq, Buffer.from(await N.gerarZipBlob(lista).arrayBuffer()));
  try { execFileSync('unzip', ['-tq', zipArq]); } catch (e) { ok(false, 'unzip -t: ' + e.stdout); }
  const dentro = execFileSync('unzip', ['-p', zipArq, 'APPS/TEKKEN3/TEKKEN3.BIN']);
  ok(Buffer.compare(dentro, Buffer.from(dadosF1.subarray(1000, 4000))) === 0, 'fatia certa dentro do ZIP');
  ok(execFileSync('unzip', ['-p', zipArq, 'APPS/TEKKEN3/title.cfg']).toString() === N.TITLE_CFG, 'title.cfg');
  fs.unlinkSync(zipArq);

  // 5. o disco de verdade (opcional)
  const pasta = process.argv[2];
  if (pasta && fs.existsSync(path.join(pasta, 'Tekken 3 (USA) (Track 1).bin'))) {
    const faixa = (n) => fs.openAsBlob(path.join(pasta, `Tekken 3 (USA) (Track ${n}).bin`));
    const [b1, b2, b3] = await Promise.all([faixa(1), faixa(2), faixa(3)]);
    let r = await N.conferir([comNome(b1, 'Tekken 3 (USA) (Track 1).bin'), comNome(b2, 'Tekken 3 (USA) (Track 2).bin'), comNome(b3, 'Tekken 3 (USA) (Track 3).bin')]);
    ok(/SLUS_004\.02$/i.test(r.jogo.boot) && r.crc1Ok && r.crc2Ok, 'disco em faixas: ' + N.hex8(r.crc1) + ' ' + N.hex8(r.crc2));
    r = await N.conferir([comNome(new Blob([b1, b2, b3]), 'Tekken 3 (USA).bin')]);
    ok(r.crc1Ok && r.crc2Ok, 'disco num .bin só');
    r = await N.conferir([comNome(b1, 'so_a_faixa1.bin')]);
    ok(r.crc1Ok && r.f2 === null, 'só a faixa 1');
    console.log('disco de verdade conferido');
  } else console.log('(sem a pasta do disco: testes do disco de verdade pulados)');

  console.log(falhas ? `criador: ${falhas} falha(s)` : 'criador: testes OK');
  process.exit(falhas ? 1 : 0);
})().catch((e) => { console.log('ERRO', e); process.exit(1); });
