#!/usr/bin/env python3
"""Lê o host:spu_trace.bin (make SPU_TRACE=1) e lista cada voz ligada (KON):
quadro, voz, pitch, endereço, ADSR e quanto tempo ela durou até o KOFF do
jogo ou o ENDX (fim da amostra), e o ENVX ao longo da vida.
Uso: python3 tools/comparar_spu_trace.py TRACE [TRACE2] [--de Q] [--ate Q]"""
import struct, sys

def ler(nome):
    d = open(nome, 'rb').read()
    mag, nq, nk, ver = struct.unpack_from('<4I', d, 0)
    assert mag == 0x54555053, 'não é spu_trace'
    p = 16; qs = []
    for _ in range(nq):
        vb, kon, koff, endx = struct.unpack_from('<4I', d, p)
        envx = struct.unpack_from('<24H', d, p + 16)
        if ver >= 2:
            extra = struct.unpack_from('<3I', d, p + 64); p += 76
        else:
            extra = (0, 0, 0); p += 64
        qs.append((vb, kon, koff, endx, envx) + extra)
    ks = []
    for _ in range(nk):
        vb, voz, pitch, ssa, a1, a2, vl, vr, lsa = struct.unpack_from('<I8H', d, p)
        ks.append(dict(vb=vb, voz=voz, pitch=pitch, ssa=ssa, a1=a1, a2=a2, vl=vl, vr=vr, lsa=lsa)); p += 20
    return qs, ks

def vidas(qs, ks):
    out = []
    for k in ks:
        v, b = k['voz'], 1 << k['voz']
        fim = None; motivo = '?'; env = []
        for q in qs[k['vb'] + 1:]:
            vb, kon, koff, endx, envx = q[:5]
            env.append(envx[v])
            if kon & b: fim = vb; motivo = 'novo KON'; break
            if koff & b: fim = vb; motivo = 'KOFF'; break
            if endx & b: fim = vb; motivo = 'ENDX'; break
        k = dict(k); k['dur'] = (fim - k['vb']) if fim is not None else None
        k['motivo'] = motivo; k['envmax'] = max(env) if env else 0
        k['env'] = env[:12]
        out.append(k)
    return out

def mostrar(nome, de, ate):
    qs, ks = ler(nome)
    print(f'== {nome}: {len(qs)} quadros, {len(ks)} KON')
    for k in vidas(qs, ks):
        if not (de <= k['vb'] <= ate): continue
        print(f"q{k['vb']:5d} v{k['voz']:2d} pitch {k['pitch']:04x} ssa {k['ssa']:04x} lsa {k['lsa']:04x} "
              f"adsr {k['a1']:04x}/{k['a2']:04x} vol {k['vl']:04x}/{k['vr']:04x} -> {k['dur']} q ({k['motivo']}) "
              f"envmax {k['envmax']:04x} env {' '.join('%x' % e for e in k['env'])}")

if __name__ == '__main__':
    args = [a for a in sys.argv[1:]]
    de, ate = 0, 1 << 30
    if '--de' in args: i = args.index('--de'); de = int(args[i + 1]); del args[i:i + 2]
    if '--ate' in args: i = args.index('--ate'); ate = int(args[i + 1]); del args[i:i + 2]
    for a in args: mostrar(a, de, ate)
