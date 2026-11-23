#!/usr/bin/env python3
"""Resumo de um log do port (console ou PCSX2) numa tela, ou comparação de dois.

Uso (de Codigo/ps2/):
  python3 tools/resumo_log.py LOG                resumo de um log
  python3 tools/resumo_log.py ANTES DEPOIS       comparação (ex.: V44 x V45)
  python3 tools/resumo_log.py LOG --janelas      + tabela janela a janela (~2 s cada)

O que mostra:
- versão (linha "log: versao") e o "bench:" do boot; autoteste com diferença
  aparece como FALHA;
- luta: mediana das janelas de ~2 s em que a lógica da luta rodou
  ("logica: atualizacoes da luta" > 0): relógio do round, quadros, IRQs do
  timer 2, VBlanks perdidos, interrupção, GTE, ciclos por bloco, 80036CAC...;
- GTE por comando (linha "gte:", V45 em diante) e as funções que mais gastam;
- linha do tempo ("diag:"), média dos quadros da luta: passo 1, passo 2,
  callback do timer 2, listas, VBlank e o EE por passo com desenho
  (passo 1 + timer 2 + VBlank), que precisa caber em 16,7 ms para 60 imagens/s.
"""
import os, re, statistics, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import linha_tempo  # noqa: E402

META_MS = 1000 / 60  # um VBlank do PS1 (NTSC)

# medidas da luta: (chave, rótulo, unidade, casas decimais)
MEDIDAS = [
    ('relogio', 'relógio do round (PS1 = 100)', '%', 1),
    ('quadros', 'quadros por janela (~2 s)', '', 0),
    ('q1', '  de 1 VBlank', '', 0),
    ('q2', '  de 2 VBlanks', '', 0),
    ('q3', '  de 3+ VBlanks', '', 0),
    ('pior', '  pior quadro', 'ms', 1),
    ('irqs', 'IRQs do timer 2 vistas pelo jogo', '', 0),
    ('perdidos', 'VBlanks do PS1 perdidos por atraso', '', 0),
    ('interrupcao', 'interrupção', '%', 0),
    ('gte_pct', 'GTE', '%', 0),
    ('gte_vb', 'comandos do GTE por VBlank', '', 0),
    ('ciclos_bloco', 'ciclos do EE por bloco do PS1', '', 0),
    ('n_36cac', '80036cac (nativa)', '%', 0),
    ('gpu_pol', 'ciclos de GPU por polígono', '', 0),
    ('gsr', 'gsr_poligono ciclos/polígono', '', 0),
    ('redesenhos', 'redesenhos iguais pulados', '', 0),
]

PADROES = [
    (r'perfil: interrupcao (\d+)%', ('interrupcao',)),
    (r'relogio: \d+ passos do relogio do round em \d+ VBlanks do PS1 \(([\d.]+)%\)', ('relogio',)),
    (r'tempo: \d+ VBlanks reais .*perdidos por atraso: (\d+)', ('perdidos',)),
    (r'prof: gte (\d+)% \((\d+)/VB\)', ('gte_pct', 'gte_vb')),
    (r'ritmo: .*irqs vistas (\d+)', ('irqs',)),
    (r'timer2: escritas \d+ \| quadros: (\d+) \(1 VBlank (\d+), 2 VBlanks (\d+), 3\+ (\d+)\), pior ([\d.]+) ms',
     ('quadros', 'q1', 'q2', 'q3', 'pior')),
    (r'velocidade: \d+ blocos do PS1/s, (\d+) ciclos do EE por bloco', ('ciclos_bloco',)),
    (r'logica: atualizacoes da luta \(8002ae58\) (\d+)', ('luta',)),
    (r'desenho: comandos \d+% \(dos quais gsr_poligono \d+%, (\d+) ciclos/poligono\)', ('gsr',)),
    (r'perfil: gs paginas .*ciclos GPU/poligono (\d+)', ('gpu_pol',)),
    (r'perfil: OTs limpas .*redesenhos iguais pulados (\d+)', ('redesenhos',)),
]
PADROES = [(re.compile(p), c) for p, c in PADROES]


def carregar_nomes():
    """Nomes das funções: docs/FUNCOES_PRINCIPAIS.md (curados) e, se existir, a
    lista do Ghidra (referencia/, fora do git; só nomes de biblioteca)."""
    ps2 = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    res = {}
    try:
        with open(os.path.join(ps2, '..', 'referencia', 'ghidra_proj', 'saida', 'funcoes.csv'), errors='replace') as f:
            csv = f.read().splitlines()
        for l in csv:
            p = l.split(';')
            if len(p) > 1 and re.fullmatch(r'[0-9A-Fa-f]{8}', p[0]) and not re.match(r'(FUN|SYS_OBJ|LAB)_', p[1]):
                res[p[0].lower()] = p[1]
    except OSError:
        pass
    try:
        with open(os.path.join(ps2, 'docs', 'FUNCOES_PRINCIPAIS.md'), errors='replace') as f:
            doc = f.read().splitlines()
        for l in doc:
            c = [x.strip() for x in l.split('|')]
            if len(c) < 4:
                continue
            ends = re.findall(r'\b[0-9a-f]{8}\b', c[1])
            nms = re.findall(r'`([^`]+)`', c[2])
            if c[2].startswith('`') and len(nms) == len(ends):
                res.update(zip(ends, nms))
            elif len(ends) == 1 and not c[2].startswith('`'):
                txt = re.split(r'[(`]', c[2])[0].strip()
                if txt:
                    res[ends[0]] = txt[:24]
    except OSError:
        pass
    return res


NOMES = carregar_nomes()


def nome_func(a):
    return '%s %s' % (a, NOMES[a]) if a in NOMES else a


def ler(caminho):
    """Lê o log: cabeçalho, bench, janelas de ~2 s e quadros da linha do tempo."""
    with open(caminho, errors='replace') as f:
        linhas = f.read().splitlines()
    log = {'arquivo': os.path.basename(caminho), 'versao': '?', 'compilado': '',
           'origem': 'console', 'bench': [], 'janelas': []}
    jan = None
    for bruta in linhas:
        if bruta.startswith('[') and ('Program Path:' in bruta or 'PCSX2' in bruta):
            log['origem'] = 'PCSX2'
        l = re.sub(r'^\[[^\]]*\]\s*', '', bruta)
        m = re.match(r'log: versao (.*?), compilado em (.*)$', l)
        if m:
            log['versao'], log['compilado'] = m.group(1), m.group(2)
            continue
        m = re.match(r'Tekken 3 PS2 - (T3 PS2 .*)$', l)  # no PCSX2 não há "log: versao"
        if m and log['versao'] == '?':
            log['versao'] = m.group(1).strip()
            continue
        if l.startswith('bench: '):
            log['bench'].append(l[7:].strip())
            continue
        if l.startswith('perfil: interrupcao'):
            jan = {'nat': {}, 'func': {}, 'gte': {}, 'nat_gte': {}}
            log['janelas'].append(jan)
        if jan is None or l.startswith('diag:'):   # diag: é da linha do tempo, lida abaixo
            continue
        for rx, chaves in PADROES:
            m = rx.match(l)
            if m:
                for k, v in zip(chaves, m.groups()):
                    jan[k] = float(v)
                break
        if l.startswith('nativas gte ('):   # V46: parte de cada nativa que é GTE
            for a, p in re.findall(r'([0-9a-f]{8}):(\d+)%', l.split('):', 1)[-1]):
                jan['nat_gte'][a] = float(p)
        elif l.startswith('nativas ('):
            for a, p in re.findall(r'([0-9a-f]{8}):(\d+)%/\d+/\d+', l):
                jan['nat'][a] = float(p)
            jan['n_36cac'] = jan['nat'].get('80036cac')
        elif l.startswith('funcoes ('):
            for a, p in re.findall(r'([0-9a-f]{8}):([\d.]+)', l.split('):', 1)[-1]):
                jan['func'][a] = float(p)
        elif l.startswith('gte:'):
            for nome, n, c, p in re.findall(r'(\w+) (\d+)/VB (\d+) c \((\d+)%\)', l):
                jan['gte'][nome] = (float(n), float(c), float(p))
    log['luta'] = [j for j in log['janelas'] if j.get('luta', 0) > 0]
    # linha do tempo: quadros com passo da luta, de todos os despejos
    qs = []
    for ev in linha_tempo.despejos(linhas):
        qs += [q for q in linha_tempo.quadros(ev) if q['passos'] > 0]
    log['quadros_diag'] = qs
    return log


def mediana(janelas, k):
    v = [j[k] for j in janelas if j.get(k) is not None]
    return statistics.median(v) if v else None


def media(v):
    return sum(v) / len(v) if v else None


def fmt(v, casas=0):
    if v is None:
        return '-'
    return ('%.' + str(casas) + 'f') % v


def dif(a, b, un=''):
    """Diferença de b sobre a: em pontos se a medida já é %, senão relativa."""
    if a is None or b is None:
        return ''
    if un == '%':
        return '%+.1f p.p.' % (b - a)
    if a == 0:
        return ''
    return '%+.0f%%' % ((b - a) * 100 / a)


def bench(log):
    """{rótulo: (texto, números, falha)} do bench do boot. Rótulo = texto sem
    parênteses até o 1º número solto ("gte RTPT", "gsr_poligono"); uma linha toda
    entre parênteses complementa a anterior ("(caminho antigo: 753 ciclos)")."""
    res, ant = {}, ''
    for t in log['bench']:
        compl = t.startswith('(') and t.endswith(')')
        s = re.sub(r'\([^)]*\)', '', t[1:-1] if compl else t)
        rot = re.split(r'\b\d+\b', s)[0].strip(' :,')
        if compl:
            rot = '%s (%s)' % (ant, rot)
        else:
            ant = rot
        nums = [int(x) for x in re.findall(r'\b\d+\b', s)]
        falha = falhou(t)
        if rot in res:   # rótulo repetido (log com dois boots): fica o 1º, mas FALHA não some
            if falha and not res[rot][2]:
                res[rot] = (t, nums, True)
        else:
            res[rot] = (t, nums, falha)
    return res


def falhou(t):
    m = re.search(r'(\d+) diferencas', t)
    return bool(m and int(m.group(1)) != 0)


def gte_cmds(log):
    """Média na luta por comando do GTE: (chamadas/VB, ciclos por chamada, %).
    Ciclos por chamada: média pesada pelas chamadas de cada janela. O log divide
    as chamadas por 120 em inteiro ("0/VB" = 0 a 119 chamadas), então o peso é
    vb + 0,5, o meio da faixa: comando raro não some nem fica com peso zero."""
    soma, n = {}, len(log['luta'])
    for j in log['luta']:
        for nome, (vb, c, p) in j['gte'].items():
            s = soma.setdefault(nome, {'vb': 0.0, 'peso': 0.0, 'ciclos': 0.0, 'pct': 0.0})
            s['vb'] += vb
            s['peso'] += vb + 0.5
            s['ciclos'] += (vb + 0.5) * c
            s['pct'] += p
    return {k: (s['vb'] / n, s['ciclos'] / s['peso'], s['pct'] / n) for k, s in soma.items()}


def funcoes(log, campo='func'):
    """Média na luta do % de cada função (ausente numa janela conta 0)."""
    soma, n = {}, len(log['luta'])
    for j in log['luta']:
        for a, p in j[campo].items():
            soma[a] = soma.get(a, 0) + p
    return {a: s / n for a, s in soma.items()}


def diag(log):
    """Médias da linha do tempo nos quadros da luta (ms): 'todos', '2' (quadros com
    passo de recuperação, o normal hoje) e '1' (1 passo: abertura do round, KO, ou
    a luta inteira quando o passo couber num VBlank)."""
    res = {}
    for nome, filtro in (('todos', lambda q: True), ('2', lambda q: q['passos'] >= 2),
                         ('1', lambda q: q['passos'] == 1)):
        qs = [q for q in log['quadros_diag'] if filtro(q)]
        if not qs:
            res[nome] = None
            continue
        d = {k: media([q[k] for q in qs]) for k in
             ('quadro', 'espera', 'p1', 'p2', 't2_1a', 't2_resto', 'listas', 'vb')}
        d['ee'] = media([q['p1'] + q['t2'] + q['vb'] for q in qs])
        d['n'] = len(qs)
        res[nome] = d
    return res if res['todos'] else None


DIAG_ROT = [('quadro', 'quadro'), ('espera', 'espera ociosa'), ('p1', 'passo 1 (com desenho)'),
            ('p2', 'passo 2 (recuperação)'), ('t2_1a', 'timer 2: 1ª volta'),
            ('t2_resto', 'timer 2: voltas seguintes'), ('listas', '  listas (dentro do timer 2)'),
            ('vb', 'VBlank'), ('ee', 'EE por passo com desenho')]


def cabecalho(log):
    comp = ' (compilado em %s)' % log['compilado'] if log['compilado'] else ''
    print('== %s: %s%s, %s' % (log['arquivo'], log['versao'], comp, log['origem']))
    print('   janelas de ~2 s: %d, com luta: %d; quadros da luta na linha do tempo: %d'
          % (len(log['janelas']), len(log['luta']), len(log['quadros_diag'])))


def tabela_janelas(log):
    print('\nJanela a janela (luta = passos da lógica da luta na janela):')
    print('%4s %5s %8s %7s %5s %8s %6s %5s %6s %8s' % ('#', 'luta', 'relogio', 'quadros', 'irqs',
          'perdidos', 'interr', 'gte', 'c/bloco', '80036cac'))
    for i, j in enumerate(log['janelas']):
        print('%4d %5s %7s%% %7s %5s %8s %5s%% %4s%% %6s %7s%%' % (
            i, fmt(j.get('luta')), fmt(j.get('relogio'), 1), fmt(j.get('quadros')), fmt(j.get('irqs')),
            fmt(j.get('perdidos')), fmt(j.get('interrupcao')), fmt(j.get('gte_pct')),
            fmt(j.get('ciclos_bloco')), fmt(j.get('n_36cac'))))


def resumo(log):
    cabecalho(log)
    if log['bench']:
        print('\nBoot (bench):')
        for t in log['bench']:
            print('  %s%s' % ('FALHA: ' if falhou(t) else '', t))
    if log['luta']:
        print('\nLuta, mediana de %d janelas:' % len(log['luta']))
        for k, rot, un, casas in MEDIDAS:
            v = mediana(log['luta'], k)
            if v is not None:
                print('  %-40s %8s %s' % (rot, fmt(v, casas), un))
        g = gte_cmds(log)
        if g:
            print('\nGTE por comando (média na luta): chamadas/VB, ciclos por chamada, % do tempo')
            for nome, (vb, c, p) in sorted(g.items(), key=lambda x: -x[1][2]):
                print('  %-6s %6.0f/VB %6.0f c %5.1f%%' % (nome, vb, c, p))
        f = funcoes(log)
        if f:
            print('\nFunções que mais gastam (média na luta, % do tempo, inclui o que chamam):')
            for a, p in sorted(f.items(), key=lambda x: -x[1])[:10]:
                print('  %-40s %6.1f%%' % (nome_func(a), p))
        nat = funcoes(log, 'nat')
        if nat:
            ng = {}
            for a in nat:
                v = [j['nat_gte'][a] for j in log['luta'] if a in j['nat_gte']]
                ng[a] = statistics.median(v) if v else None
            print('Nativas (média na luta, %; entre parênteses a parte que é GTE): ' + ' | '.join(
                '%s %.0f%s' % (nome_func(a), p, '' if ng[a] is None else ' (GTE %.0f%%)' % ng[a])
                for a, p in sorted(nat.items(), key=lambda x: -x[1])))
    else:
        print('\n(sem janela de luta neste log)')
    d = diag(log)
    if d:
        cols = [('todos', 'todos'), ('2', '2 passos'), ('1', '1 passo')]
        print('\nLinha do tempo (quadros da luta), média em ms:')
        print('  %-40s' % '' + ''.join('%10s' % c for _, c in cols))
        print('  %-40s' % 'quadros' + ''.join('%10s' % (d[k]['n'] if d[k] else 0) for k, _ in cols))
        for k, rot in DIAG_ROT:
            print('  %-40s' % rot + ''.join('%10s' % fmt(d[c] and d[c][k], 2) for c, _ in cols))
        falta = d['todos']['ee'] - META_MS
        print('  meta para 60 imagens/s: %.1f ms por passo com desenho -> %s' % (
            META_MS, 'faltam %.1f ms' % falta if falta > 0 else 'cabe (sobram %.1f ms)' % -falta))


def linha_cmp(rot, a, b, casas=0, un=''):
    sufixo = lambda v: fmt(v, casas) + (un if v is not None and un == '%' else (' ' + un if v is not None and un else ''))
    print('  %-40s %10s %10s %10s' % (rot[:40], sufixo(a), sufixo(b), dif(a, b, un)))


def comparar(la, lb):
    cabecalho(la)
    cabecalho(lb)
    print('\n  %-40s %10s %10s %10s' % ('', 'ANTES', 'DEPOIS', 'dif'))
    ba, bb = bench(la), bench(lb)
    if ba or bb:
        print('Boot (bench, ciclos do EE):')
        for rot in list(ba) + [c for c in bb if c not in ba]:
            ta, tb = ba.get(rot), bb.get(rot)
            if 'diferencas' in ((ta or tb)[0]):
                st = lambda t: '-' if t is None else ('FALHA' if t[2] else 'ok')
                print('  %-40s %10s %10s' % (rot[:40], st(ta), st(tb)))
            elif ta and tb and len(ta[1]) == 1 and len(tb[1]) == 1:
                linha_cmp(rot, ta[1][0], tb[1][0])
            else:
                junta = lambda t: '/'.join(str(n) for n in t[1]) if t else '-'
                print('  %-40s %s -> %s' % (rot[:40], junta(ta), junta(tb)))
    if la['luta'] or lb['luta']:
        print('Luta (mediana das janelas: %d antes, %d depois):' % (len(la['luta']), len(lb['luta'])))
        for k, rot, un, casas in MEDIDAS:
            a, b = mediana(la['luta'], k), mediana(lb['luta'], k)
            if a is not None or b is not None:
                linha_cmp(rot, a, b, casas, un)
        ga, gb = gte_cmds(la), gte_cmds(lb)
        if ga or gb:
            print('GTE por comando (ciclos por chamada; chamadas/VB entre parênteses):')
            for nome in sorted(set(ga) | set(gb), key=lambda x: -max(ga.get(x, (0, 0, 0))[2], gb.get(x, (0, 0, 0))[2])):
                a, b = ga.get(nome), gb.get(nome)
                print('  %-40s %10s %10s %10s' % (
                    '%s (%s / %s)' % (nome, fmt(a and a[0]), fmt(b and b[0])),
                    fmt(a and a[1]), fmt(b and b[1]), dif(a and a[1], b and b[1])))
        fa, fb = funcoes(la), funcoes(lb)
        if fa or fb:
            print('Funções (% do tempo na luta):')
            topo = sorted(set(fa) | set(fb), key=lambda x: -max(fa.get(x, 0), fb.get(x, 0)))[:10]
            for a in topo:
                linha_cmp(nome_func(a), fa.get(a), fb.get(a), 1, '%')
    da, db = diag(la), diag(lb)
    if da or db:
        print('Linha do tempo, média de todos os quadros da luta:')
        for c, rot in (('todos', 'quadros'), ('2', '  com 2 passos'), ('1', '  com 1 passo')):
            na = da[c]['n'] if da and da[c] else 0
            nb = db[c]['n'] if db and db[c] else 0
            print('  %-40s %10d %10d' % (rot, na, nb))
        ta, tb = da and da['todos'], db and db['todos']
        for k, rot in DIAG_ROT:
            linha_cmp(rot, ta and ta[k], tb and tb[k], 2, 'ms')
        print('  meta para 60 imagens/s: %.1f ms por passo com desenho' % META_MS)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    opcoes = [a for a in sys.argv[1:] if a.startswith('--')]
    if not 1 <= len(args) <= 2 or any(o != '--janelas' for o in opcoes):
        sys.exit(__doc__)
    logs = [ler(a) for a in args]
    if len(logs) == 1:
        resumo(logs[0])
    else:
        comparar(*logs)
    if '--janelas' in sys.argv:
        for log in logs:
            print('\n' + log['arquivo'])
            tabela_janelas(log)


if __name__ == '__main__':
    main()
