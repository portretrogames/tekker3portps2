#!/usr/bin/env python3
"""Resumo das janelas do PMC (make PMC=1, runtime/pmc.h) de um log.

Uso: python3 tools/pmc_resumo.py [-r] log.txt [log2.txt ...]
  -r  so o resumo das janelas de luta: eventos e ciclos por poligono e por
      passo da logica, em cada regiao (jogo, nativas, lista, irq...)

No relatorio a linha "pmc:" sai ANTES de "quadros:", "logica:" e "perfil: gs"
da mesma janela: os poligonos/quadros/passos de cada janela sao os que vem
depois dela.
"""
import re, sys, statistics as st

def ler(f):
    J = []; cur = None
    for l in open(f, errors='replace'):
        l = re.sub(r'^\[\s*[\d.]+\]\s*', '', l)
        if l.startswith('pmc:'):
            cfg = re.search(r'\[(.*?) \|\s*(.*?)\]', l)
            if not cfg:                     # linha cortada (fim do log)
                cur = None
                continue
            d = {m.group(1): (int(m.group(2)), int(m.group(3)), int(m.group(4)))
                 for m in re.finditer(r'\| (\w+) (\d+)/(\d+) em (\d+)', l)}
            cur = dict(ev0=cfg.group(1).strip(), ev1=cfg.group(2).strip(), pol=None, qd=None, lu=None, d=d)
            J.append(cur)
            continue
        if cur is None:
            continue
        m = re.match(r'perfil: gs .*poligonos (\d+)', l)
        if m and cur['pol'] is None: cur['pol'] = int(m.group(1))
        m = re.match(r'quadros: (\d+) \(1 VBlank (\d+), 2 VBlanks (\d+), 3\+ (\d+)', l)
        if m and cur['qd'] is None: cur['qd'] = tuple(int(x) for x in m.groups())
        m = re.search(r'atualizacoes da luta \(8002ae58\) (\d+)', l)
        if m and cur['lu'] is None: cur['lu'] = int(m.group(1))
    return J

resumo = '-r' in sys.argv
for f in [a for a in sys.argv[1:] if a != '-r']:
    J = ler(f)
    print('==', f)
    if not resumo:
        for j in J:
            d = j['d']
            print(f"{j['ev0'][:22]:22s} pol {j['pol']} quadros {j['qd']} passos {j['lu']} | " +
                  ' '.join(f"{k}:{v[0]}/{v[1]}/{v[2]}" for k, v in d.items()))
    luta = [j for j in J if j['pol'] and j['pol'] > 30000 and j['lu'] and j['lu'] >= 80 and j['qd']]
    if not luta:
        print('  (sem janelas de luta)'); continue
    print('  %d janelas de luta; quadros/janela med %.0f (em 1 VBlank med %.0f); poligonos/quadro med %.0f' % (
        len(luta), st.median(j['qd'][0] for j in luta), st.median(j['qd'][1] for j in luta),
        st.median(j['pol'] / max(1, j['qd'][0]) for j in luta)))
    for reg in ('jogo', 'nativas', 'lista', 'irq', 'runtime', 'espera'):
        js_reg = [j for j in luta if reg in j['d']]
        if not js_reg:
            continue
        lin = []
        for ev in sorted(set(j['ev0'] for j in js_reg)):
            js = [j for j in js_reg if j['ev0'] == ev]
            a = st.mean(j['d'][reg][0] * 1000.0 / j['pol'] for j in js)
            b = st.mean(j['d'][reg][1] * 1000.0 / j['pol'] for j in js)
            lin.append('%s %.1f' % (ev[:14], a))
            lin.append('%s %.1f' % (js[0]['ev1'][:20], b))
        cic = st.mean(j['d'][reg][2] * 1e6 / j['pol'] for j in js_reg)
        cpp = st.mean(j['d'][reg][2] * 1e6 / j['lu'] for j in js_reg)
        print('  %-8s por poligono: %6.0f ciclos | por passo: %7.0f ciclos | %s' % (reg, cic, cpp, ' | '.join(lin)))
