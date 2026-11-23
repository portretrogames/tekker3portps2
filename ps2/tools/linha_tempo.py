#!/usr/bin/env python3
"""Linha do tempo 'diag:' do port (V41d/V42): resumo quadro a quadro.
Uso: python3 linha_tempo.py LOG [n_despejo]
Colunas: duração do quadro (fim da espera -> fim da espera seguinte), espera,
contador do timer 2 no teste de recuperação (800295F4), passos da lógica,
duração do passo 1 e do passo 2, chamadas da IRQ do timer 2 no quadro (1ª =
a que libera o quadro), listas desenhadas inteiras (e ms), handler do VBlank.
Também é módulo: tools/resumo_log.py usa despejos() e quadros()."""
import re, sys


def despejos(linhas):
    """Despejos 'diag: linha do tempo' do log; cada um = [(t em us, evento, valor)]."""
    desp, cur = [], None
    for l in linhas:
        l = re.sub(r'^\[[^\]]*\]\s*', '', l)
        if l.startswith('diag: linha do tempo'):
            cur = []; desp.append(cur); continue
        m = re.match(r'diag:\s+(\d+) (.{14}) ([0-9a-f]{8})$', l)
        if m and cur is not None:
            cur.append((int(m.group(1)), m.group(2).strip(), int(m.group(3), 16)))
    return desp


def quadros(ev):
    """Quadros de um despejo (de 'espera fim' ao 'espera fim' seguinte), tempos em ms."""
    blocos, q = [], None
    for t, e, a in ev:
        if e == 'espera fim':
            if q: q['fim'] = t; blocos.append(q)
            q = {'ini': t, 'flag': a, 'ev': []}
        elif q: q['ev'].append((t, e, a))
    res = []
    for q in blocos:
        luta = [t for t, e, a in q['ev'] if e == 'luta']
        esp = [t for t, e, a in q['ev'] if e == 'espera ini']
        rec = [a for t, e, a in q['ev'] if e == 'checa recup']
        t_esp_ini = esp[-1] if esp else q['fim']
        h, t2, vb, li, l0 = None, [], [], [], None
        for t, e, a in q['ev']:
            if e == 'handler ini': h = (t, a)
            elif e == 'handler fim' and h:
                (t2 if h[1] == 0x80029784 else vb if h[1] == 0x80092d34 else []).append(t - h[0]); h = None
            elif e == 'lista ini': l0 = t
            elif e == 'lista fim' and l0 is not None:
                if a >= 200: li.append(t - l0)
                l0 = None
        res.append({
            'quadro': (q['fim'] - q['ini']) / 1000,
            'espera': (q['fim'] - t_esp_ini) / 1000,
            'rec': rec,
            'passos': len(luta),
            'p1': ((luta[1] if len(luta) > 1 else t_esp_ini) - luta[0]) / 1000 if luta else 0,
            'p2': (t_esp_ini - luta[1]) / 1000 if len(luta) > 1 else 0,
            'nt2': len(t2),
            't2_1a': t2[0] / 1000 if t2 else 0,
            't2_resto': sum(t2[1:]) / 1000,
            't2': sum(t2) / 1000,
            'listas': sum(li) / 1000,
            'nlistas': len(li),
            'vb': sum(vb) / 1000,
            'otc': sum(1 for t, e, a in q['ev'] if e == 'OTC'),
        })
    return res


def main():
    uso = 'uso: python3 linha_tempo.py LOG [n_despejo]  (n: 0 = primeiro, -1 = último)'
    if not 2 <= len(sys.argv) <= 3:
        sys.exit(uso)
    try:
        qual = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    except ValueError:
        sys.exit(uso)
    with open(sys.argv[1], errors='replace') as f:
        L = f.read().splitlines()
    desp = despejos(L)
    if not desp:
        sys.exit('nenhum despejo "diag: linha do tempo" neste log')
    if not -len(desp) <= qual < len(desp):
        sys.exit('n_despejo fora da faixa: este log tem %d despejos (0 a %d, ou -1 a -%d)'
                 % (len(desp), len(desp) - 1, len(desp)))
    ev = desp[qual]
    print('despejos:', len(desp), '| eventos neste:', len(ev), '| tempos em ms')
    hdr = '%4s %6s %6s %5s %5s %12s %4s %15s %13s %6s %5s' % ('#', 'quadro', 'espera', 'recup', 'passo', 'p1 / p2', 'T2n', 'T2 1a + resto', 'listas(n)', 'VB', 'OTC')
    print(hdr)
    soma = {}
    for i, f in enumerate(quadros(ev)):
        print('%4d %6.1f %6.1f %5s %5d %5.1f /%5.1f %4d %6.2f +%6.2f %8.2f(%2d) %6.2f %5d' % (
            i, f['quadro'], f['espera'], ','.join(str(r) for r in f['rec']) or '-', f['passos'],
            f['p1'], f['p2'], f['nt2'], f['t2_1a'], f['t2_resto'], f['listas'], f['nlistas'],
            f['vb'], f['otc']))
        for k in ('quadro', 'espera', 'p1', 'p2', 't2', 'listas', 'vb', 'nlistas', 'nt2'):
            soma.setdefault(k, []).append(f[k])
    n = len(soma.get('quadro', []))
    if n:
        print('media (%d quadros): ' % n + ', '.join('%s %.2f' % (k, sum(v) / len(v)) for k, v in soma.items()))


if __name__ == '__main__':
    main()
