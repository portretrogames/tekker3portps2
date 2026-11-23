#!/usr/bin/env python3
"""Extrai a fonte msx (8x8, 256 letras, 2 KB) da libdebug do PS2SDK para um
header C, para o teste da abertura no PC (tests/abertura_test.c).

Uso: python3 tools/msx_fonte.py LIBDEBUG.a OBJCOPY SAIDA.h
"""
import os, subprocess, sys, tempfile

if len(sys.argv) != 4:
    sys.exit(__doc__)
lib, objcopy, saida = sys.argv[1:4]
with tempfile.TemporaryDirectory() as d:
    subprocess.run(["ar", "x", os.path.abspath(lib), "font.o"], cwd=d, check=True)
    binario = os.path.join(d, "font.bin")
    subprocess.run([objcopy, "-O", "binary", "--only-section=.rodata", os.path.join(d, "font.o"), binario],
                   check=True)
    with open(binario, "rb") as f:
        dados = f.read()
# font.o só tem a msx (2 KB, no início da .rodata); se o PS2SDK mudar isso, para
if len(dados) != 2048:
    sys.exit(f"msx_fonte: .rodata de font.o com {len(dados)} bytes (esperava 2048 = só a msx)")
with open(saida, "w") as f:
    f.write("/* gerado por tools/msx_fonte.py a partir da libdebug do PS2SDK */\n")
    f.write("unsigned char msx[2048] = {\n")
    for i in range(0, 2048, 16):
        f.write("    " + ", ".join(f"0x{b:02x}" for b in dados[i:i + 16]) + ",\n")
    f.write("};\n")
