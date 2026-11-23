#!/usr/bin/env python3
"""
nativo.py — traduz rotinas "folha" do executável do PS1 (sem jal/j, terminam
em jr $ra) para C nativo otimizado para o EE.

Diferente do código gerado pelo psxrecomp (CPS, registradores em cpu->gpr,
volta ao despacho a cada chamada e checagem de interrupção a cada desvio),
aqui:
  - os registradores do PS1 viram variáveis locais (o gcc aloca em registros);
  - a rotina inteira roda de uma vez (sem despacho, sem checar interrupção —
    a interrupção pendente é atendida no próximo despacho);
  - memória pelo caminho rápido de fast_memory.h (RAM, scratchpad, I/O) e
    GTE em linha (gte_regs.h) ou gte_execute.
Semântica igual à do código do psxrecomp (que é o que roda hoje): slot de
atraso dos desvios sim; atraso de load NÃO (o valor carregado aparece na
hora, como no código gerado — ex.: mfc2 no slot em 800374FC).

Uso: nativo.py <SLUS_004.02 ou .bin> <saida.c> <inicio:fim> [...]
     (ou listas .txt; "inicio:fim lba:base" = rotina de overlay, só com o .bin)
"""
import struct, sys

BASE = 0x80010000


def ler_exe(caminho):
    d = open(caminho, 'rb').read(16)
    if d[:8] == b'PS-X EXE':
        return open(caminho, 'rb').read()
    # imagem .bin (2352 bytes/setor, modo 2): procura TEKKEN3\SLUS_004.02
    f = open(caminho, 'rb')

    def setor(l):
        f.seek(l * 2352)
        return f.read(2352)[24:24 + 2048]

    pvd = setor(16)
    lba, tam = struct.unpack('<I', pvd[158:162])[0], struct.unpack('<I', pvd[166:170])[0]

    def achar(lba, tam, nome):
        pos = 0
        while pos < tam:
            s = setor(lba + pos // 2048)
            o = 0
            while o < 2048 and s[o]:
                n = s[o + 32]
                if s[o + 33:o + 33 + n].decode('latin1').split(';')[0] == nome:
                    return struct.unpack('<I', s[o + 2:o + 6])[0], struct.unpack('<I', s[o + 10:o + 14])[0]
                o += s[o]
            pos += 2048
        sys.exit('nativo: %s nao encontrado na imagem' % nome)

    d = achar(lba, tam, 'TEKKEN3')
    e = achar(d[0], d[1], 'SLUS_004.02')
    return b''.join(setor(e[0] + i) for i in range((e[1] + 2047) // 2048))[:e[1]]


class Rotina:
    def __init__(self, exe, ini, fim, cod=None):
        self.exe, self.ini, self.fim, self.cod = exe, ini, fim, cod

    def word(self, a):
        if self.cod is not None:                 # V48: overlay (bytes de ini a fim+4)
            o = a - self.ini
            return struct.unpack('<I', self.cod[o:o + 4])[0]
        o = 0x800 + a - BASE
        return struct.unpack('<I', self.exe[o:o + 4])[0]


def ler_overlay(caminho, lba, base, ini, fim):
    """V48: bytes de [ini, fim+4) de um overlay que o jogo carrega do disco
    (setores de 2048 bytes a partir de 'lba', carregados em 'base').
    None se a entrada for só o executável (sem o disco não há overlay: o
    interpretador atende esses endereços)."""
    if ini < base or fim < ini or (ini | fim | base) & 3:
        sys.exit('nativo: faixa de overlay invalida %08x:%08x (base %08x)' % (ini, fim, base))
    out = bytearray()
    with open(caminho, 'rb') as f:
        if f.read(8) == b'PS-X EXE':
            return None
        a = ini
        while a < fim + 4:
            off = a - base
            s, o = lba + off // 2048, off % 2048
            n = min(2048 - o, fim + 4 - a)
            f.seek(s * 2352 + 24 + o)
            out += f.read(n)
            a += n
    if len(out) != fim + 4 - ini:
        sys.exit('nativo: overlay curto em %08x' % ini)
    return bytes(out)


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


def R(n):
    return '0u' if n == 0 else 'r%d' % n


# ---------------------------------------------------------------- decodifica
def decodificar(w, a):
    """Devolve dict: tipo ('alu','load','store','branch','jr','gte','nop'),
    leituras (regs), escrita (reg ou None), codigo C (para alu/load/...)."""
    op = w >> 26
    rs = (w >> 21) & 31
    rt = (w >> 16) & 31
    rd = (w >> 11) & 31
    sa = (w >> 6) & 31
    fn = w & 63
    imm = w & 0xFFFF
    simm = sx16(imm)
    I = {'w': w, 'a': a, 'le': set(), 'esc': None}

    def alu(dst, expr, le):
        I.update(tipo='alu', esc=dst, le=set(le), c=('%s = %s;' % (R(dst), expr)) if dst else '')
        return I

    if w == 0:
        I['tipo'] = 'nop'
        return I
    if op == 0:
        if fn == 0:  return alu(rd, '%s << %d' % (R(rt), sa), [rt])
        if fn == 2:  return alu(rd, '%s >> %d' % (R(rt), sa), [rt])
        if fn == 3:  return alu(rd, '(uint32_t)((int32_t)%s >> %d)' % (R(rt), sa), [rt])
        if fn == 4:  return alu(rd, '%s << (%s & 31u)' % (R(rt), R(rs)), [rt, rs])
        if fn == 6:  return alu(rd, '%s >> (%s & 31u)' % (R(rt), R(rs)), [rt, rs])
        if fn == 7:  return alu(rd, '(uint32_t)((int32_t)%s >> (%s & 31u))' % (R(rt), R(rs)), [rt, rs])
        if fn == 8:
            I.update(tipo='jr', le={rs}, rs=rs)
            return I
        if fn in (0x20, 0x21): return alu(rd, '%s + %s' % (R(rs), R(rt)), [rs, rt])
        if fn in (0x22, 0x23): return alu(rd, '%s - %s' % (R(rs), R(rt)), [rs, rt])
        if fn == 0x24: return alu(rd, '%s & %s' % (R(rs), R(rt)), [rs, rt])
        if fn == 0x25: return alu(rd, '%s | %s' % (R(rs), R(rt)), [rs, rt])
        if fn == 0x26: return alu(rd, '%s ^ %s' % (R(rs), R(rt)), [rs, rt])
        if fn == 0x27: return alu(rd, '~(%s | %s)' % (R(rs), R(rt)), [rs, rt])
        if fn == 0x2A: return alu(rd, '((int32_t)%s < (int32_t)%s)' % (R(rs), R(rt)), [rs, rt])
        if fn == 0x2B: return alu(rd, '(%s < %s)' % (R(rs), R(rt)), [rs, rt])
        if fn == 0x10: I.update(tipo='alu', esc=rd, le=set(), c='%s = hi;' % R(rd) if rd else '', usa_hilo=1); return I
        if fn == 0x12: I.update(tipo='alu', esc=rd, le=set(), c='%s = lo;' % R(rd) if rd else '', usa_hilo=1); return I
        if fn == 0x18:
            I.update(tipo='alu', le={rs, rt}, usa_hilo=1,
                     c='{ int64_t p = (int64_t)(int32_t)%s * (int32_t)%s; lo = (uint32_t)p; hi = (uint32_t)((uint64_t)p >> 32); }' % (R(rs), R(rt)))
            return I
        if fn == 0x19:
            I.update(tipo='alu', le={rs, rt}, usa_hilo=1,
                     c='{ uint64_t p = (uint64_t)%s * %s; lo = (uint32_t)p; hi = (uint32_t)(p >> 32); }' % (R(rs), R(rt)))
            return I
        raise ValueError('SPECIAL %02x em %08x' % (fn, a))
    if op == 1:
        cond = {0: '(int32_t)%s < 0', 1: '(int32_t)%s >= 0'}.get(rt)
        if cond is None:
            raise ValueError('REGIMM %d em %08x' % (rt, a))
        I.update(tipo='branch', le={rs}, cond=cond % R(rs), alvo=(a + 4 + (simm << 2)) & 0xFFFFFFFF)
        return I
    if op in (4, 5, 6, 7):
        c = {4: '%s == %s' % (R(rs), R(rt)), 5: '%s != %s' % (R(rs), R(rt)),
             6: '(int32_t)%s <= 0' % R(rs), 7: '(int32_t)%s > 0' % R(rs)}[op]
        I.update(tipo='branch', le={rs, rt} if op in (4, 5) else {rs}, cond=c,
                 alvo=(a + 4 + (simm << 2)) & 0xFFFFFFFF)
        return I
    if op in (2, 3):
        raise ValueError('j/jal em %08x (rotina nao e folha)' % a)
    if op in (8, 9):  return alu(rt, '%s + 0x%Xu' % (R(rs), simm & 0xFFFFFFFF), [rs])
    if op == 0xA:     return alu(rt, '((int32_t)%s < %d)' % (R(rs), simm), [rs])
    if op == 0xB:     return alu(rt, '(%s < 0x%Xu)' % (R(rs), simm & 0xFFFFFFFF), [rs])
    if op == 0xC:     return alu(rt, '%s & 0x%Xu' % (R(rs), imm), [rs])
    if op == 0xD:     return alu(rt, '%s | 0x%Xu' % (R(rs), imm), [rs])
    if op == 0xE:     return alu(rt, '%s ^ 0x%Xu' % (R(rs), imm), [rs])
    if op == 0xF:     return alu(rt, '0x%08Xu' % (imm << 16), [])
    end = '%s + 0x%Xu' % (R(rs), simm & 0xFFFFFFFF)
    if op in (0x20, 0x21, 0x23, 0x24, 0x25):
        f = {0x20: 'ps2_jan_rd8s(%s)',
             0x24: 'ps2_jan_rd8(%s)',
             0x21: 'ps2_jan_rd16s(%s)',
             0x25: 'ps2_jan_rd16(%s)',
             0x23: 'ps2_jan_rd32(%s)'}[op]
        I.update(tipo='load', esc=rt if rt else None, le={rs}, expr=f % end)
        return I
    if op in (0x28, 0x29, 0x2B):
        f = {0x28: 'ps2_jan_wr8(%s, (uint8_t)%s);', 0x29: 'ps2_jan_wr16(%s, (uint16_t)%s);',
             0x2B: 'ps2_jan_wr32(%s, %s);'}[op]
        I.update(tipo='store', le={rs, rt}, c=f % (end, R(rt)))
        return I
    if op == 0x32:  # lwc2
        I.update(tipo='gte', le={rs}, c='gte_wr_data_i(cpu, %d, ps2_jan_rd32(%s));' % (rt, end))
        return I
    if op == 0x3A:  # swc2
        I.update(tipo='gte', le={rs}, c='ps2_jan_wr32(%s, gte_rd_data_i(cpu, %d));' % (end, rt))
        return I
    if op == 0x10:   # COP0 (V48: o decodificador do vídeo mexe no SR)
        if rs == 0:
            I.update(tipo='load', esc=rt if rt else None, le=set(), expr='cpu->cop0[%d]' % rd)
            return I
        if rs == 4:
            I.update(tipo='gte', le={rt}, c='cpu->cop0[%d] = %s;' % (rd, R(rt)))
            return I
        raise ValueError('COP0 %08x nao suportada em %08x' % (w, a))
    if op == 0x12:
        if w & (1 << 25):
            k = '%X' % (w & 0x1FFFFFF)
            if k in GTE_K:   # V30: comando especializado (gte.c)
                I.update(tipo='gte', c='gte_k_%s(cpu);' % k)
            else:
                I.update(tipo='gte', c='gte_execute(cpu, 0x%07Xu);' % (w & 0x1FFFFFF))
            return I
        if rs == 0:   # mfc2: atraso como load
            I.update(tipo='load', esc=rt if rt else None, le=set(), expr='gte_rd_data_i(cpu, %d)' % rd)
            return I
        if rs == 2:
            I.update(tipo='load', esc=rt if rt else None, le=set(), expr='gte_read_ctrl(cpu, %d)' % rd)
            return I
        if rs == 4:
            I.update(tipo='gte', le={rt}, c='gte_wr_data_i(cpu, %d, %s);' % (rd, R(rt)))
            return I
        if rs == 6:
            I.update(tipo='gte', le={rt}, c='gte_write_ctrl(cpu, %d, %s);' % (rd, R(rt)))
            return I
    raise ValueError('instrucao %08x nao suportada em %08x' % (w, a))


# ---------------------------------------------------------------- traduz
def traduzir(exe, ini, fim, nome, cod=None):
    rot = Rotina(exe, ini, fim, cod)
    ins = {}
    a = ini
    while a <= fim:
        ins[a] = decodificar(rot.word(a), a)
        a += 4
    alvos = {ini}
    for a, I in ins.items():
        if I['tipo'] == 'branch':
            if not (ini <= I['alvo'] <= fim):
                raise ValueError('desvio para fora da rotina em %08x -> %08x' % (a, I['alvo']))
            alvos.add(I['alvo'])
    # instruções em slot de atraso (de desvio) não podem ser desvio
    for a, I in ins.items():
        if I['tipo'] in ('branch', 'jr') and ins.get(a + 4, {}).get('tipo') in ('branch', 'jr'):
            raise ValueError('desvio no slot de atraso em %08x' % (a + 4))

    usados, escritos, hilo = set(), set(), False
    for I in ins.values():
        usados |= I['le']
        if I.get('esc'):
            usados.add(I['esc']); escritos.add(I['esc'])
        hilo = hilo or I.get('usa_hilo', 0)
    usados.discard(0)

    def codigo_simples(I):
        """Código de uma instrução que não é desvio, sem atraso de load."""
        t = I['tipo']
        if t == 'nop':
            return ''
        if t == 'load':
            return ('%s = %s;' % (R(I['esc']), I['expr'])) if I['esc'] else '(void)%s;' % I['expr']
        return I['c']

    out = []
    p = out.append
    p('/* %s: rotina folha 0x%08X-0x%08X traduzida por tools/nativo.py */' % (nome, ini, fim))
    p('void %s(CPUState *cpu)' % nome)
    p('{')
    p('    uint32_t %s;' % ', '.join('r%d = cpu->gpr[%d]' % (r, r) for r in sorted(usados)))
    if hilo:
        p('    uint32_t hi = cpu->hi, lo = cpu->lo;')
    a = ini
    while a <= fim:
        I = ins[a]
        if a in alvos or a == ini:
            p('L_%08X:' % a)
        t = I['tipo']
        if t in ('branch', 'jr'):
            ds = ins.get(a + 4)
            if ds is None:
                raise ValueError('slot de atraso fora da rotina em %08x' % a)
            dsc = codigo_simples(ds) if ds['tipo'] != 'load' else (
                ('%s = %s;' % (R(ds['esc']), ds['expr'])) if ds['esc'] else '(void)%s;' % ds['expr'])
            if t == 'jr':
                p('    { uint32_t alvo = %s; %s' % (R(I['rs']), dsc))
                for r in sorted(escritos):
                    p('      cpu->gpr[%d] = r%d;' % (r, r))
                if hilo:
                    p('      cpu->hi = hi; cpu->lo = lo;')
                p('      cpu->pc = alvo; return; }')
            elif I['cond'] in ('(int32_t)0u >= 0', '0u == 0u'):
                p('    { %s goto L_%08X; }' % (dsc, I['alvo']))
            else:
                p('    { int c = %s; %s if (c) goto L_%08X; goto L_%08X; }' % (I['cond'], dsc, I['alvo'], a + 8))
                alvos.add(a + 8)
            # o slot de atraso só roda "solto" se for alvo de algum desvio
            if a + 4 in alvos:
                p('L_%08X:' % (a + 4))
                p('    %s' % dsc)
                if t == 'branch':
                    p('    goto L_%08X;' % (a + 8))
                    alvos.add(a + 8)
            a += 8
            continue
        c = codigo_simples(I)
        if c:
            p('    %s' % c)
        a += 4
    p('    /* fim sem jr: não deveria chegar aqui */')
    p('    cpu->pc = 0x%08Xu;' % (fim + 4))
    p('}')
    # tira rótulos não usados (evita aviso)
    txt = '\n'.join(out)
    import re
    usados_lbl = set(re.findall(r'goto (L_[0-9A-F]{8})', txt))
    linhas = [l for l in out if not (re.fullmatch(r'L_[0-9A-F]{8}:', l) and l[:-1] not in usados_lbl)]
    return '\n'.join(linhas) + '\n'


GTE_K = set('108041B 118043F 138041C 1400006 145545C 14B465D 178000C 180001 190003D 198003D 280030 41E012 486012 49E012 4A3268 4A465C 4A6412 A00428 A80428'.split())   # igual a GTE_LISTA_K em runtime/gte_regs.h

def main():
    exe = ler_exe(sys.argv[1])
    saida = sys.argv[2]
    partes = ['/* Gerado por tools/nativo.py a partir do executavel do jogo: NAO distribuir. */',
              '#include <stdint.h>', '#include "cpu_state.h"', '#include "fast_memory.h"', '#include "janela.h"', '#include "gte_regs.h"',
              '#include "nativo_overlay.h"',
              'extern void gte_execute(CPUState *cpu, uint32_t cmd);',
              'extern uint32_t gte_read_ctrl(CPUState *cpu, uint8_t reg);',
              'extern void gte_write_ctrl(CPUState *cpu, uint8_t reg, uint32_t v);', '']
    rotinas = []
    for arg in sys.argv[3:]:
        if '/' in arg or arg.endswith('.txt'):      # arquivo com a lista
            for l in open(arg):
                l = l.split('#')[0].strip()
                if l:
                    rotinas.append(l)
        else:
            rotinas.append(arg)
    partes.append('int ps2_nat_executar(CPUState *cpu, uint32_t ini, void (*nat)(CPUState *));')
    overlays = []
    for arg in rotinas:
        campos = arg.split()
        if len(campos) not in (1, 2):
            sys.exit('nativo: linha invalida: %r' % arg)
        i, f = campos[0].split(':')
        i, f = int(i, 16), int(f, 16)
        if len(campos) == 1:            # rotina do executável: gancho no código gerado
            partes.append(traduzir(exe, i, f, 'ps2_nat_%08X' % i))
            partes.append('int ps2_nat_%08X_gancho(CPUState *cpu) { return ps2_nat_executar(cpu, 0x%08Xu, ps2_nat_%08X); }\n' % (i, i, i))
            continue
        # V48: rotina de overlay "inicio:fim lba:base". Vai para a tabela que o
        # interpretador consulta; o código esperado vai junto para conferir que
        # a RAM ainda tem ESTE overlay antes de usar a versão nativa.
        lba, base = campos[1].split(':')
        cod = ler_overlay(sys.argv[1], int(lba), int(base, 16), i, f)
        if cod is None:
            sys.stderr.write('nativo: %08X e de overlay e a entrada e o executavel: fica no interpretador\n' % i)
            continue
        partes.append(traduzir(exe, i, f, 'ps2_nat_%08X' % i, cod))
        pal = struct.unpack('<%dI' % (len(cod) // 4), cod)
        partes.append('static const uint32_t ps2_ovl_%08X_cod[%d] = {' % (i, len(pal)))
        for k in range(0, len(pal), 6):
            partes.append('    ' + ' '.join('0x%08Xu,' % v for v in pal[k:k + 6]))
        partes.append('};\n')
        overlays.append((i, len(pal)))
    partes.append('/* V48: rotinas de overlay (runtime/interp.c, runtime/nativo_overlay.h) */')
    if overlays:
        partes.append('const Ps2NatOverlay ps2_nat_overlays[] = {')
        for i, n in overlays:
            partes.append('    {0x%08Xu, %du, ps2_ovl_%08X_cod, ps2_nat_%08X},' % (i, n, i, i))
        partes.append('};')
    else:
        partes.append('const Ps2NatOverlay ps2_nat_overlays[1] = {{0u, 0u, 0, 0}};')
    partes.append('const unsigned ps2_nat_n_overlays = %du;\n' % len(overlays))
    open(saida, 'w').write('\n'.join(partes))


if __name__ == '__main__':
    main()
