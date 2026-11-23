#!/usr/bin/env python3
"""Extrai o executável SLUS_004.02 do .bin do Tekken 3 (MODE2/2352) para análise."""
import os
import struct
import sys

BIN = os.path.expanduser("~/PROJETOS/tekken 3 ps2/ps2/Tekken 3 (USA) (Track 1).bin")
OUT = os.path.expanduser("~/PROJETOS/tekken 3 ps2/referencia/exe/SLUS_004.02")

f = open(BIN, "rb")


def sec(n):
    f.seek(n * 2352)
    return f.read(2352)[24:24 + 2048]


pvd = sec(16)
if pvd[1:6] != b"CD001":
    sys.exit("PVD não encontrado")
root = pvd[156:156 + 34]
lba = struct.unpack("<I", root[2:6])[0]
size = struct.unpack("<I", root[10:14])[0]
def listar(lba, size):
    d = b"".join(sec(lba + k) for k in range((size + 2047) // 2048))
    i = 0
    while i < len(d):
        ln = d[i]
        if ln == 0:
            i = (i // 2048 + 1) * 2048
            continue
        nl = d[i + 32]
        name = d[i + 33:i + 33 + nl].decode("latin1")
        el = struct.unpack("<I", d[i + 2:i + 6])[0]
        es = struct.unpack("<I", d[i + 10:i + 14])[0]
        if d[i + 25] & 2 and nl > 1:                # subpasta
            listar(el, es)
        elif name.startswith("SLUS"):
            data = b"".join(sec(el + k) for k in range((es + 2047) // 2048))[:es]
            os.makedirs(os.path.dirname(OUT), exist_ok=True)
            open(OUT, "wb").write(data)
            print(name, es, data[:8])
        i += ln


listar(lba, size)
