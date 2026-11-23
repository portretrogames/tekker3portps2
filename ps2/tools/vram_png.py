#!/usr/bin/env python3
"""Converte dumps brutos da VRAM do PS1 em PNGs."""

import argparse
from pathlib import Path

from PIL import Image

PS2 = Path(__file__).resolve().parent.parent
LARGURA, ALTURA = 1024, 512


def converter(origem: Path, destino: Path) -> None:
    dados = origem.read_bytes()
    esperado = LARGURA * ALTURA * 2
    if len(dados) != esperado:
        raise ValueError(f"{origem}: esperado {esperado} bytes, recebido {len(dados)}")
    rgb = bytearray(LARGURA * ALTURA * 3)
    for i in range(LARGURA * ALTURA):
        pixel = dados[2 * i] | (dados[2 * i + 1] << 8)
        for canal, deslocamento in enumerate((0, 5, 10)):
            componente = (pixel >> deslocamento) & 31
            rgb[3 * i + canal] = (componente << 3) | (componente >> 2)
    destino.parent.mkdir(parents=True, exist_ok=True)
    Image.frombytes("RGB", (LARGURA, ALTURA), bytes(rgb)).save(destino)
    print(f"{origem} -> {destino}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dumps", nargs="*", type=Path,
                        help="dumps de VRAM (padrão: 20, 40 e 80 segundos)")
    args = parser.parse_args()
    for origem in args.dumps or [PS2 / f"vram_dump_{t}.bin" for t in (20, 40, 80)]:
        nome = "vram.png" if origem.name == "vram_dump.bin" else origem.stem + ".png"
        converter(origem, PS2 / "build" / nome)


if __name__ == "__main__":
    main()
