#!/usr/bin/env python3
"""Gera as artes do OPL do port a partir das imagens que o usuário pôs em
"covers/" (capa, disco e logo oficiais; não vão para o git).

O OPL procura as artes de um app pelo nome do ELF do boot= do title.cfg
(src/appsupport.c: appGetImage usa appGetItemStartup), não pelo título:
ART/tekken3.elf_COV.png etc. Tamanhos iguais aos do port SMW PS2.

Uso: python3 tools/gerar_artes_opl.py PASTA_COVERS PASTA_ART [CAPTURA_DA_LUTA.jpg]
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageEnhance

BOOT = "tekken3.elf"


def ajusta(im, w, h):
    """Recorta no centro para a proporção w:h e redimensiona."""
    r = w / h
    W, H = im.size
    if W / H > r:
        nw = int(H * r)
        im = im.crop(((W - nw) // 2, 0, (W - nw) // 2 + nw, H))
    else:
        nh = int(W / r)
        im = im.crop((0, (H - nh) // 2, W, (H - nh) // 2 + nh))
    return im.resize((w, h), Image.LANCZOS)


def main():
    covers, art = Path(sys.argv[1]), Path(sys.argv[2])
    captura = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    art.mkdir(parents=True, exist_ok=True)
    caixa = Image.open(covers / "ps1_tekken3_2.jpg").convert("RGB")
    disco = Image.open(covers / "5483116-tekken-3-playstation-media.jpg").convert("RGB")
    logo = Image.open(covers / "Tekken_3.webp").convert("RGBA")

    # caixa: verso | lombada (faixa escura) | frente
    g = np.asarray(caixa.convert("L"), dtype=float).mean(axis=0)
    W, H = caixa.size
    meio = range(int(W * 0.40), int(W * 0.60))
    escuras = [x for x in meio if g[x] < 40]
    fim_verso, ini_frente = min(escuras), max(escuras) + 1
    frente = caixa.crop((ini_frente + 6, 0, W, H))
    verso = caixa.crop((0, 0, fim_verso - 6, H))
    saidas = {}

    saidas["COV"] = ajusta(frente, 140, 200)
    saidas["COV2"] = ajusta(verso, 242, 344)

    # ícone: o disco, fora do círculo transparente
    d = ajusta(disco, 64, 64).convert("RGBA")
    mascara = Image.new("L", (256, 256), 0)
    ImageDraw.Draw(mascara).ellipse((2, 2, 253, 253), fill=255)
    d.putalpha(mascara.resize((64, 64), Image.LANCZOS))
    saidas["ICO"] = d

    # logo: inteiro, centrado em 300x125 transparente
    lg = logo.copy()
    lg.thumbnail((300, 125), Image.LANCZOS)
    tela = Image.new("RGBA", (300, 125), (0, 0, 0, 0))
    tela.paste(lg, ((300 - lg.width) // 2, (125 - lg.height) // 2), lg)
    saidas["LGO"] = tela

    # fundo: a arte da frente (sem o selo de cima), escurecida, 640x480
    arte = frente.crop((0, int(frente.height * 0.06), frente.width, int(frente.height * 0.90)))
    saidas["BG"] = ImageEnhance.Brightness(ajusta(arte, 640, 480)).enhance(0.55)

    # tela do jogo: captura da placa (1280x720, a imagem do PS2 no meio)
    if captura and captura.exists():
        c = Image.open(captura).convert("RGB")
        c = c.crop((82, 52, 1220, 680))                   # área do jogo na captura
        saidas["SCR"] = ajusta(c, 250, 188)

    for suf, im in saidas.items():
        destino = art / f"{BOOT}_{suf}.png"
        im.save(destino)
        print(destino.name, im.size)


if __name__ == "__main__":
    main()
