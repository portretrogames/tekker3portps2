"""Linhas 'trab: w/p ...' do log (make TRAB_QUADRO=1): trabalho/período por quadro, décimos de ms.
uso: python3 tools/trab_quadro.py LOG. Cruza: quadros que levaram 2 VBlanks (período 20-37 ms) e o trabalho deles."""
import sys
vistos, q = set(), []
for l in open(sys.argv[1], errors="replace"):
    if l.startswith("trab:") and l not in vistos:
        vistos.add(l)
        par = [tuple(int(y) / 10 for y in x.split("/")) for x in l.split()[1:] if "/" in x]
        if len(par) >= 90: q += par
um = [w for w, p in q if p < 20]
dois = [w for w, p in q if 20 <= p < 37]
tres = [(w, p) for w, p in q if p >= 37]
print(f"quadros {len(q)}: 1 VBlank {len(um)}, 2 VBlanks {len(dois)}, 3+ {len(tres)}")
def faixas(v):
    f = {"<12": 0, "12-14": 0, "14-16": 0, "16-17": 0, ">17": 0}
    for w in v:
        k = "<12" if w < 12 else "12-14" if w < 14 else "14-16" if w < 16 else "16-17" if w < 17 else ">17"
        f[k] += 1
    return f
print("trabalho dos quadros de 1 VBlank:", faixas(um))
print("trabalho dos quadros de 2 VBlanks:", faixas(dois))
dur = sorted(p for w, p in q if 20 <= p < 37)
if dur: print(f"duração dos de 2 VBlanks: mediana {dur[len(dur)//2]:.1f} ms")
# quadro de 2 VBlanks seguido/antecedido de quadro curto? (padrão 33 + 0 ms = ritmo)
curtos = sum(1 for w, p in q if p < 12)
print(f"quadros com período < 12 ms (dois quadros num VBlank só): {curtos}")
