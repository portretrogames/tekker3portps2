"""Gera runtime/cartao_icone.h: o icone 3D (.icn) e o icon.sys do save do
Tekken 3 PS2 no Memory Card do PS2 (V59).

Copiado do gerador do port SMW PS2 (Fonte/gerar_icone.py), com outro desenho.
Sem icon.sys e icone, o navegador do PS2 mostra a pasta como "dados
corrompidos". Arte original (nada da Namco): um memory card cinza generico,
levemente girado, com rotulo escrito "T3 PS2". Formato conferido com saves comerciais: cabecalho de 20 bytes,
coordenadas em 1/4096 (Y negativo para cima), quadros de animacao de 8 bytes
e textura 128x128 ABGR1555 sem compressao (tipo 0x07).
"""
import math
import struct
from pathlib import Path

W = H = 128
ICON_VERSION = b"T3ICON1"  # muda quando o desenho muda: o jogo regrava o icone
BODY, DARK = (150, 150, 158), (95, 95, 105)
FONT = {
    'T': ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    '3': ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    'S': ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    'W': ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    'P': ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    '2': ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    ' ': ["00000"] * 7,
}
LABEL_V = 120 / 128          # o rotulo usa as linhas 0-119; abaixo ficam as cores do cartucho
UV_BODY = (124 / 128, 124 / 128)
UV_DARK = (116 / 128, 124 / 128)

def texture():
    px = [[(0, 0, 0)] * W for _ in range(H)]
    def rect(x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(H, y1)):
            for x in range(max(0, x0), min(W, x1)):
                px[y][x] = c
    for y in range(H):  # fundo do rotulo: degrade azul escuro
        t = y / 120
        for x in range(W):
            px[y][x] = (int(20 + 30 * t), int(30 + 40 * t), int(90 + 70 * t))
    rect(0, 0, W, 4, (230, 40, 50))      # frisos vermelhos
    rect(0, 116, W, 120, (230, 40, 50))
    def text(t, y, scale, color, shadow):
        x = 64 - (len(t) * 6 * scale - scale) // 2
        for ch in t:
            for r, row in enumerate(FONT[ch]):
                for c, bit in enumerate(row):
                    if bit == '1':
                        X, Y = x + c * scale, y + r * scale
                        rect(X + 2, Y + 2, X + scale + 2, Y + scale + 2, shadow)
                        rect(X, Y, X + scale, Y + scale, color)
            x += 6 * scale
    text("T3", 14, 7, (255, 220, 60), (10, 10, 30))
    text("PS2", 68, 6, (255, 255, 255), (10, 10, 30))
    rect(0, 120, W, 128, BODY)           # cores do corpo do cartucho
    rect(112, 120, 120, 128, DARK)
    out = bytearray()
    for row in px:
        for r, g, b in row:
            out += struct.pack('<H', (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 | 0x8000)
    return bytes(out), px

def quad(corners, normal, uvs):
    """Dois triangulos, mais as copias viradas (visiveis dos dois lados)."""
    tris = []
    for a, b, c in ((0, 1, 2), (0, 2, 3)):
        tri = [(corners[i], uvs[i]) for i in (a, b, c)]
        tris.append((tri, normal))
        tris.append(([tri[0], tri[2], tri[1]], tuple(-v for v in normal)))
    return tris

def box(x0, x1, yt, yb, zf, zb, uv):
    u = [uv] * 4
    return (quad([(x0, yt, zf), (x1, yt, zf), (x1, yb, zf), (x0, yb, zf)], (0, 0, -1), u) +
            quad([(x1, yt, zb), (x0, yt, zb), (x0, yb, zb), (x1, yb, zb)], (0, 0, 1), u) +
            quad([(x0, yt, zb), (x0, yt, zf), (x0, yb, zf), (x0, yb, zb)], (-1, 0, 0), u) +
            quad([(x1, yt, zf), (x1, yt, zb), (x1, yb, zb), (x1, yb, zf)], (1, 0, 0), u) +
            quad([(x0, yt, zb), (x1, yt, zb), (x1, yt, zf), (x0, yt, zf)], (0, -1, 0), u) +
            quad([(x0, yb, zf), (x1, yb, zf), (x1, yb, zb), (x0, yb, zb)], (0, 1, 0), u))

def cartridge():
    """Memory card generico, mais alto que largo: corpo cinza, conector
    escuro embaixo e rotulo so com "T3 PS2"."""
    tris = box(-1.1, 1.1, -2.5, 0.0, -0.18, 0.18, UV_BODY)              # corpo
    tris += box(-0.9, 0.9, -0.12, 0.08, -0.12, 0.12, UV_DARK)           # conector
    for i in range(3):                                                  # frisos de pegada
        y = -2.3 + i * 0.1
        tris += box(-0.8, 0.8, y, y + 0.04, -0.22, -0.18, UV_DARK)
    lab = [(0, 0), (1, 0), (1, LABEL_V), (0, LABEL_V)]
    zl = -0.2
    tris += quad([(-0.95, -1.95, zl), (0.95, -1.95, zl), (0.95, -0.3, zl), (-0.95, -0.3, zl)], (0, 0, -1), lab)
    return tris

def rotate(tris, yaw):
    c, s = math.cos(yaw), math.sin(yaw)
    rot = lambda v: (v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c)
    return [([(rot(p), uv) for p, uv in tri], rot(n)) for tri, n in tris]

def icn():
    f16 = lambda v: max(-32768, min(32767, int(round(v * 4096))))
    tris = rotate(cartridge(), math.radians(-18))   # levemente girado
    out = bytearray(struct.pack('<IIIfI', 0x10000, 1, 0x07, 1.0, len(tris) * 3))
    for tri, n in tris:
        for (x, y, z), (u, v) in tri:
            out += struct.pack('<hhhH', f16(x), f16(y), f16(z), 0x400)
            out += struct.pack('<hhhH', f16(n[0]), f16(n[1]), f16(n[2]), 0x400)
            out += struct.pack('<hh', f16(u), f16(v))
            out += bytes((255, 255, 255, 255))
    out += struct.pack('<IIfII', 1, 16, 1.0, 0, 1)                  # cabecalho da animacao
    out += struct.pack('<II', 0, 1) + struct.pack('<ff', 1.0, 1.0)  # 1 quadro, 1 chave
    out += texture()[0]
    return bytes(out)

def sjis(s):
    out = bytearray()
    for ch in s:
        o = ord(ch)
        if ch == ' ': out += b'\x81\x40'
        elif '0' <= ch <= '9': out += struct.pack('>H', 0x824F + o - 48)
        elif 'A' <= ch <= 'Z': out += struct.pack('>H', 0x8260 + o - 65)
        elif 'a' <= ch <= 'z': out += struct.pack('>H', 0x8281 + o - 97)
        else: raise ValueError(ch)
    return bytes(out)

def icon_sys(icon_name):
    line1, line2 = "Tekken 3", "PS2 Retrogames"
    title = sjis(line1) + sjis(line2)
    assert len(title) <= 68
    d = bytearray(964)
    d[0:4] = b'PS2D'
    struct.pack_into('<HHII', d, 4, 0, len(sjis(line1)), 0, 0x60)
    for i, c in enumerate([(20, 32, 90, 0), (120, 20, 30, 0), (120, 20, 30, 0), (20, 32, 90, 0)]):
        struct.pack_into('<4i', d, 16 + 16 * i, *c)
    for i, v in enumerate([(0.5, 0.5, 0.5, 0.0), (0.0, -0.4, -0.1, 0.0), (-0.5, -0.5, 0.5, 0.0)]):
        struct.pack_into('<4f', d, 80 + 16 * i, *v)
    for i, v in enumerate([(0.3, 0.3, 0.3, 0.0), (0.4, 0.4, 0.4, 0.0), (0.5, 0.5, 0.5, 0.0)]):
        struct.pack_into('<4f', d, 128 + 16 * i, *v)
    struct.pack_into('<4f', d, 176, 0.5, 0.5, 0.5, 0.0)
    d[192:192 + len(title)] = title
    for off in (260, 324, 388):
        d[off:off + len(icon_name)] = icon_name.encode()
    d[452:452 + len(ICON_VERSION)] = ICON_VERSION   # area reservada: marca a versao do desenho
    return bytes(d)

def c_array(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] __attribute__((aligned(64))) = {{"]
    for i in range(0, len(data), 24):
        lines.append("  " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)

if __name__ == '__main__':
    here = Path(__file__).resolve().parent.parent
    icon, sysf = icn(), icon_sys("t3ps2.icn")
    (here / "runtime" / "cartao_icone.h").write_text(
        "/* Gerado por tools/gerar_icone.py: icone do save no Memory Card do PS2.\n"
        " * Incluido so por cartao_ps2.c (os dados ficam aqui mesmo). */\n"
        "#ifndef PS2_CARTAO_ICONE_H\n#define PS2_CARTAO_ICONE_H\n#include <stdint.h>\n"
        "#define T3_ICONE_NOME \"t3ps2.icn\"\n" + c_array("kT3Icone", icon) + "\n" + c_array("kT3IconeSys", sysf) + "\n#endif\n")
    print(len(icon), len(sysf))
