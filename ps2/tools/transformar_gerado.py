#!/usr/bin/env python3
"""Copia um arquivo de ../gerado para build/r/ com ajustes de desempenho.
gerado/ nunca é editado; só a cópia compilada muda.

1. "CPUState* restrict cpu" nas definições func_*: sem isso o compilador supõe
   que cada escrita na RAM do PS1 pode alterar cpu->gpr[] e relê tudo.
2. No despacho: cache direto (PC -> entrada) na frente da busca binária na
   tabela de 22.325 funções. A tabela é constante, então o cache nunca fica
   errado; a validação de código alterado (overlays) continua igual.
3. "cpu->read_word(" / "cpu->write_word(" (e half/byte) viram chamadas
   diretas às funções rápidas de fast_memory.h (antes eram macros que
   reescreviam o "cpu->", sem parênteses externos: auditoria V19 3.3).
   Também vale para SLUS_004.02_decls.h (psx_swl/psx_swr), copiado para
   build/r/ — o #include "..." dos shards acha a cópia primeiro.
"""
import os, re, sys

ent, sai = sys.argv[1], sys.argv[2]
s = open(ent, encoding='utf-8').read()
# 6. GTE em linha (V23): mtc2/mfc2/lwc2 sem chamada de função; o gancho
# de precisão de vértices (vazio no PS2) some.
s = re.sub(r'gte_precision_store_word\([^;]*\);', '', s)
s = s.replace('psx_cyc_lwc2_read(cpu, ', 'ps2_fast_read_word(')
s = s.replace('gte_write_data(cpu, ', 'gte_wr_data_i(cpu, ')
s = s.replace('gte_read_data(cpu, ', 'gte_rd_data_i(cpu, ')
# V30: comandos GTE com opcode constante -> wrapper especializado (gte.c)
_GTE_K = set('108041B 118043F 138041C 1400006 145545C 14B465D 178000C 180001 190003D 198003D 280030 41E012 486012 49E012 4A3268 4A465C 4A6412 A00428 A80428'.split())
def _gte_k(m):
    k = m.group(1).upper().lstrip('0') or '0'
    return 'gte_k_%s(cpu);' % k if k in _GTE_K else m.group(0)
s = re.sub(r'gte_execute\(cpu, 0x([0-9A-Fa-f]+)\);', _gte_k, s)
s, n_mem = re.subn(r'\bcpu->(read|write)_(word|half|byte)\(', r'ps2_fast_\1_\2(', s)
if re.search(r'cpu->(read|write)_(word|half|byte)\b', s):
    sys.exit('transformar_gerado: acesso de memoria por ponteiro que nao foi trocado')
s = re.sub(r'^(void func_[0-9A-Fa-f_]+)\(CPUState\* cpu\)', r'\1(CPUState* restrict cpu)', s, flags=re.M)

# V56: acessos relativos ao sp (gpr[29]) vão direto pela janela RAM+scratchpad
# (runtime/janela.h): sem o teste de região e sem a marcação de código sujo.
# A pilha do jogo fica sempre na RAM (ou no scratchpad) e nunca em cima de
# código. Com GEN_OPT="-O2 -DPS2_JANELA_VERIFICAR=1" cada acesso é conferido.
_BITS = {'word': '32', 'half': '16', 'byte': '8'}
s, _n_pw = re.subn(r'ps2_fast_write_(word|half|byte)\(cpu->gpr\[29\](?: \+ (-?\d+))?, ',
                   lambda m: 'ps2_pil_wr%s(cpu->gpr[29], %s, ' % (_BITS[m.group(1)], m.group(2) or '0'), s)
s, _n_pr = re.subn(r'psx_cyc_load_(word|half|byte)\(cpu, cpu->gpr\[29\](?: \+ (-?\d+))?, \d+, 0x[0-9A-Fa-f]+u\)',
                   lambda m: 'ps2_pil_rd%s(cpu->gpr[29], %s)' % (_BITS[m.group(1)], m.group(2) or '0'), s)

# 7. Rotinas nativas (V26, tools/nativo.py + runtime/nativo_ganchos.c): na
# entrada normal (cpu->pc == 0) a função chama a versão nativa.
import os
_lista = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'nativas.txt')
NATIVAS = [l.split('#')[0].strip().split(':')[0] for l in open(_lista)
           if l.split('#')[0].strip()]
for a in NATIVAS:
    cab = 'void func_%s(CPUState* restrict cpu)\n{\n' % a
    if cab in s:
        s = s.replace(cab, cab + '''    { extern int ps2_nat_%s_gancho(CPUState *cpu);
      if (cpu->pc == 0u && ps2_nat_%s_gancho(cpu)) return; }
''' % (a, a), 1)

# V48: VSync() da libetc espera o VBlank em v_wait(alvo, tempo) (80085B20), que
# desiste depois de tempo<<15 voltas e escreve "VSync: timeout". No PS1 isso
# passa de 16,7 ms; no código compilado a volta é bem mais rápida (~2,5 ms no
# PCSX2) e o VSync(0) do tocador do vídeo da abertura desistia ~5 vezes por
# segundo, antes do VBlank. Aqui o limite fica 8x maior (só na entrada normal;
# o laço guarda o limite na pilha logo no começo). O VSync passa 0 ou 1 (ou
# n-1 em VSync(n)); com o <<3 daqui e o <<15 do v_wait o tempo sobe 18 bits:
# a partir de 0x2000 chegaria a 2^31 e o limite ficaria negativo, então de
# 0x1000 para cima (com folga) fica como está.
cab = 'void func_80085B20(CPUState* restrict cpu)\n{\n'
if cab in s:
    s = s.replace(cab, cab + '    if (cpu->pc == 0u && cpu->gpr[5] < 0x1000u) cpu->gpr[5] <<= 3;   /* V48: VSync sem timeout cedo */\n', 1)

# V55: laço da luta 800508C4: o passo de recuperação (sem desenho) só roda se
# a lógica 8002AE58 devolveu 1. runtime/nativo_ganchos.c libera a recuperação
# também na apresentação do round (8002AE58 devolve 0).
_ant = re.compile(r'^\s*int _bc_800508D0\s*=\s*\(cpu->gpr\[16\]\s*!=\s*cpu->gpr\[19\]\);', re.M)
if 'block_800508D0:' in s:
    s, _n = _ant.subn('    int _bc_800508D0; { extern int ps2_luta_sem_recup(CPUState *c); _bc_800508D0 = ps2_luta_sem_recup(cpu); }', s)
    if not _n or re.search(r'int _bc_800508D0\s*=', s):
        raise SystemExit('transformar_gerado: o teste do laço da luta (800508D0) não foi trocado em todas as cópias ')

# 9. V33: "jal" com alvo constante vira chamada C direta. Se a função chamada
# voltar normalmente (cpu->pc == endereço de retorno) e nenhum código foi
# sobrescrito no meio (g_cod_ver igual), continua no bloco seguinte da própria
# função, sem passar pelo laço de despacho (2 despachos a menos por chamada).
# Só quando o alvo já foi validado (g_ps2_ver_ok); senão, caminho antigo.
def _tabela_despacho():
    d = os.path.join(os.path.dirname(os.path.abspath(ent)), 'SLUS_004.02_dispatch.c')
    t = open(d, encoding='utf-8', errors='replace').read()
    i = t.index('k_psx_game_dispatch[] = {')
    idx = {}
    for k, m in enumerate(re.finditer(r'\{0x([0-9A-F]+)u, 0x([0-9A-F]+)u, \d+u, \d+u, (func_[0-9A-F_]+)\}', t[i:])):
        ad, res, fn = m.group(1), m.group(2), m.group(3)
        if res == '00000000' and fn == 'func_' + ad:
            idx[ad] = k
    return idx
if not ent.endswith('_dispatch.c') and 'CPS jal ->' in s:
    _IDX = _tabela_despacho()
    partes = re.split(r'(?=\n(?:static )?void \w+\(CPUState\*)', s)
    n_jal = 0
    for pi, corpo in enumerate(partes):
        def _troca(m):
            global n_jal
            ini = m.start()
            alvo = m.group(1)
            lk = corpo.rfind('cpu->gpr[31] = 0x', max(0, ini - 4000), ini)
            if lk < 0 or alvo not in _IDX:
                return m.group(0)
            ret = re.match(r'cpu->gpr\[31\] = 0x([0-9A-F]{8})u;', corpo[lk:])
            if not ret:
                return m.group(0)
            r = ret.group(1)
            if ('block_%s:' % r) not in corpo:
                return m.group(0)
            n_jal += 1
            return ('{ if (__builtin_expect(g_ps2_ver_ok[%du] == g_cod_ver, 1)) { uint32_t _v = g_cod_ver; '
                    'cpu->pc = 0; func_%s(cpu); '
                    'if (cpu->pc == 0x%su && g_cod_ver == _v) { cpu->pc = 0; psx_check_interrupts_at(cpu, 0x%su); goto block_%s; } '
                    'return; } cpu->pc = 0x%su; return; }  /* V33 jal direto -> 0x%s */'
                    % (_IDX[alvo], alvo, r, r, r, alvo, alvo))
        partes[pi] = re.sub(r'cpu->pc = 0x([0-9A-F]{8})u; return;  /\* CPS jal -> 0x[0-9A-F]{8} \*/', _troca, corpo)
    s = ''.join(partes)
    sys.stderr.write('transformar_gerado: %d jal diretos\n' % n_jal)

if ent.endswith('_dispatch.c'):
    velho = 'static const PsxGameDispatchEntry* psx_game_find_entry(uint32_t addr) {'
    if velho not in s:
        sys.exit('transformar_gerado: busca do despacho nao encontrada')
    s = s.replace(velho, 'static const PsxGameDispatchEntry* psx_game_find_entry_busca(uint32_t addr) {', 1)
    i = s.index('psx_game_find_entry_busca(uint32_t addr) {')
    fim = s.index('\n}\n', i) + 3
    cache = '''
/* ps2: cache direto na frente da busca binária (tools/transformar_gerado.py) */
static const PsxGameDispatchEntry* psx_game_find_entry(uint32_t addr) {
    static struct { uint32_t addr; const PsxGameDispatchEntry* e; } cache[8192];
    uint32_t i = (addr >> 2) & 8191u;
    const PsxGameDispatchEntry* e;
    if (cache[i].e && cache[i].addr == addr) return cache[i].e;
    e = psx_game_find_entry_busca(addr);
    if (e) { cache[i].addr = addr; cache[i].e = e; }
    return e;
}
'''
    s = s[:fim] + cache + s[fim:]

    # 8. V31: despacho mais barato.
    #  - validade do código: cada entrada guarda a "versão" (g_cod_ver) em que
    #    foi conferida; só confere de novo se alguma escrita sujou um grânulo
    #    limpo desde então (ram.h: ps2_cod_marcar sobe a versão);
    #  - sem a chamada de psx_vsync_query_hle_try (no PS2 sempre devolve 0);
    #  - checagem de interrupção em linha (fast_memory.h).
    velho = """    if (entry->range_count == 0 || !dirty_ram_text_native_ok_ranges_from(
            &k_psx_game_code_ranges[entry->range_index].lo, entry->range_count, addr)) return 0;
    psx_check_interrupts_dispatch_entry(cpu, addr);"""
    if velho not in s:
        sys.exit('transformar_gerado: validacao do despacho nao encontrada')
    s = s.replace(velho, """    {
        uint32_t idx = (uint32_t)(entry - k_psx_game_dispatch);
        if (__builtin_expect(g_ps2_ver_ok[idx] != g_cod_ver, 0)) {
            uint32_t v = g_cod_ver;
            if (entry->range_count == 0 || !dirty_ram_text_native_ok_ranges_from(
                    &k_psx_game_code_ranges[entry->range_index].lo, entry->range_count, addr)) return 0;
            g_ps2_ver_ok[idx] = v;
        }
    }
    psx_check_interrupts_at(cpu, addr);""", 1)
    velho = """    {
        extern int psx_vsync_query_hle_try(CPUState* cpu, uint32_t addr);
        if (psx_vsync_query_hle_try(cpu, addr)) return 1;
    }"""
    if velho not in s:
        sys.exit('transformar_gerado: vsync_query do despacho nao encontrado')
    s = s.replace(velho, "    /* (V31: psx_vsync_query_hle_try removido: no PS2 sempre 0) */", 1)
    s = s.replace('/* ps2: cache direto na frente da busca binária', 'uint32_t g_ps2_ver_ok[PSX_GAME_DISPATCH_COUNT];   /* V31/V33 */\n/* ps2: cache direto na frente da busca binária', 1)
    # V56: as faixas de código do despacho dizem quais páginas da RAM têm
    # código compilado (ram.h: g_cod_pag)
    s += '''
/* ps2 (V56): páginas da RAM com código compilado (ver ram.h) */
void ps2_despacho_paginas_de_codigo(void)
{
    extern void ps2_cod_so_codigo(const uint32_t *faixas, uint32_t n);
    /* lida como pares {início, tamanho}, como dirty_ram_text_native_ok_ranges_from */
    _Static_assert(sizeof(k_psx_game_code_ranges[0]) == 2u * sizeof(uint32_t), "faixa = {lo, len}");
    ps2_cod_so_codigo(&k_psx_game_code_ranges[0].lo,
                      (uint32_t)(sizeof(k_psx_game_code_ranges) / sizeof(k_psx_game_code_ranges[0])));
}
'''
    if '#include "fast_memory.h"' not in s:
        s = s.replace('uint32_t g_ps2_ver_ok[', '#include "fast_memory.h"\nuint32_t g_ps2_ver_ok[', 1)

# V59: checagem de interrupção só nos desvios para TRÁS (laços) e nas
# outras saídas (chamadas, saltos por registrador). Nos desvios para a
# frente ela saía em ~82% dos desvios do jogo (~8 instruções cada, ~1,6 MB
# de código): um trecho sem laço termina logo e a interrupção é vista no
# próximo laço.
if not ent.endswith('_dispatch.c') and not ent.endswith('_decls.h'):
    linhas = s.split('\n')
    bloco = None
    tiradas = 0
    chk = re.compile(r'psx_check_interrupts_at\(cpu, 0x([0-9A-Fa-f]{8})u\);\s*')
    for i, l in enumerate(linhas):
        m = re.match(r'\s*block_([0-9A-Fa-f]{8})\s*:', l)
        if m:
            bloco = int(m.group(1), 16)
            continue
        # começo de função (qualquer variação da assinatura): esquece o bloco
        if re.match(r'\s*(?:static\s+)?(?:inline\s+)?void\s+\w+\s*\(\s*CPUState\s*\*', l):
            bloco = None
            continue
        if bloco is None or 'psx_check_interrupts_at' not in l:
            continue
        def _tira(m, i=i, l=l):
            global tiradas
            alvo = m.group(1)
            resto = l[m.end():]
            prox = linhas[i + 1] if i + 1 < len(linhas) else ''
            vai = resto.startswith('goto block_%s;' % alvo) or (not resto.strip() and prox.strip().startswith('goto block_%s;' % alvo))
            # (alvo e rótulo com o mesmo texto: o gerador escreve os dois em maiúsculas)
            if vai and int(alvo, 16) > bloco:
                tiradas += 1
                return ''
            return m.group(0)
        linhas[i] = chk.sub(_tira, l)
    s = '\n'.join(linhas)
    sys.stderr.write('transformar_gerado: %d checagens de interrupcao para a frente tiradas\n' % tiradas)

open(sai, 'w', encoding='utf-8').write(s)
