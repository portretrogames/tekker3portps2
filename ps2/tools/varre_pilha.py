#!/usr/bin/env python3
"""Lê um log de 'make PMC=1 PMC_VARRE=1' (runtime/pmc.h) e mostra, para cada
posição da pilha do EE (mod 4 KB), as faltas de cache de dados e os ciclos por
polígono nas rotinas nativas e na lista de desenho, e por quadro no código do
jogo (PMC_VARRE=2).

Uso: python3 tools/varre_pilha.py log.txt [log2.txt ...]

As janelas (relatórios de ~2 s) são separadas em 'luta' (com passos da lógica
da luta) e 'outras 3D' (demonstração, seleção, pose): cenas diferentes pesam
diferente, só compare posições dentro do mesmo grupo. Escolha a posição com
menos ciclos por polígono e ponha em runtime/quente.h (PS2_PILHA_LISTA,
PS2_PILHA_NATIVAS).
"""
import re
import statistics as st
import sys


def ler(caminho):
    janelas = []
    cur = None
    for l in open(caminho, errors='replace'):
        l = re.sub(r'^\[\s*[\d.]+\]\s*', '', l)
        if l.startswith('pmc:'):
            d = {m.group(1): (int(m.group(2)), int(m.group(3)), int(m.group(4)))
                 for m in re.finditer(r'\| (\w+) (\d+)/(\d+) em (\d+)', l)}
            m = re.search(r'\| pilha (?:extra )?(\d+)', l)
            cur = dict(pilha=int(m.group(1)) if m else -1, pol=None, quadros=None, passos=None, d=d)
            janelas.append(cur)
            continue
        if cur is None:
            continue
        # no relatório, estas linhas saem DEPOIS da linha pmc: da mesma janela
        m = re.match(r'perfil: gs .*poligonos (\d+)', l)
        if m and cur['pol'] is None:
            cur['pol'] = int(m.group(1))
        m = re.match(r'quadros: (\d+) \(1 VBlank (\d+)', l)
        if m and cur['quadros'] is None:
            cur['quadros'] = (int(m.group(1)), int(m.group(2)))
        m = re.search(r'atualizacoes da luta \(8002ae58\) (\d+)', l)
        if m and cur['passos'] is None:
            cur['passos'] = int(m.group(1))
    return janelas


def tabela(nome, js):
    if not js:
        return
    print('  %s: %d janelas' % (nome, len(js)))
    print('    pilha   n | nativas: faltas/pol  ciclos/pol | lista: faltas/pol  ciclos/pol | jogo: faltas/quadro  ciclos/quadro | quadros por janela (em 1 VBlank)')
    for p in sorted(set(j['pilha'] for j in js)):
        g = [j for j in js if j['pilha'] == p]

        def media(reg, i, k):
            v = [j['d'][reg][i] * k / j['pol'] for j in g if reg in j['d']]
            return st.mean(v) if v else float('nan')
        def por_quadro(reg, i, k):
            v = [j['d'][reg][i] * k / j['quadros'][0] for j in g if reg in j['d'] and j['quadros'] and j['quadros'][0]]
            return st.mean(v) if v else float('nan')
        print('    %5d %3d |        %5.1f        %5.0f |       %5.1f       %5.0f |        %6.0f       %8.0f | %s' % (
            p, len(g), media('nativas', 1, 1e3), media('nativas', 2, 1e6),
            media('lista', 1, 1e3), media('lista', 2, 1e6),
            por_quadro('jogo', 1, 1e3), por_quadro('jogo', 2, 1e6),
            ' '.join('%d(%d)' % j['quadros'] for j in g if j['quadros'])))


for caminho in sys.argv[1:]:
    J = [j for j in ler(caminho) if j['pol'] and j['pol'] > 30000 and j['pilha'] >= 0]
    print('==', caminho)
    tabela('luta', [j for j in J if (j['passos'] or 0) >= 80])
    tabela('outras 3D', [j for j in J if (j['passos'] or 0) < 80])
