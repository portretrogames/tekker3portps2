#!/usr/bin/env python3
"""Monta o Criar_Tekken3_PS2.html: um arquivo só, que abre em qualquer
navegador, com o núcleo (tools/criador/nucleo.js), o tekken3.elf do pacote e o
LEIA-ME.txt embutidos. Não usa nada do disco do jogo.

Uso (de dentro de Codigo/ps2, depois do "make pacote"):
    python3 tools/montar_criador.py [saida.html]
"""
import base64
import hashlib
import json
import re
import sys
from pathlib import Path

PS2 = Path(__file__).resolve().parent.parent
CRIADOR = PS2 / "tools" / "criador"


def main():
    saida = Path(sys.argv[1]) if len(sys.argv) > 1 else PS2 / "build" / "pacote" / "Criar_Tekken3_PS2.html"
    elf = (PS2 / "build" / "pacote" / "APPS" / "TEKKEN3" / "tekken3.elf").read_bytes()
    m = re.search(r'#define JOGO_VERSAO_NUM\s+"([^"]+)"', (PS2 / "runtime" / "jogo.h").read_text(encoding="utf-8"))
    if not m:
        raise SystemExit("não achei JOGO_VERSAO_NUM em runtime/jogo.h")
    versao = m.group(1)
    sha = hashlib.sha256(elf).hexdigest()
    b64 = base64.b64encode(elf).decode()
    b64 = "\n".join(b64[i:i + 120] for i in range(0, len(b64), 120))
    # texto JSON dentro de um <script>: sem "</" (fecharia o script) e sem "@" (as marcas)
    leia = json.dumps((PS2 / "pacote" / "LEIA-ME.txt").read_text(encoding="utf-8").replace("\n", "\r\n"))
    leia = leia.replace("</", "<\\/").replace("@", "\\u0040")
    nucleo = (CRIADOR / "nucleo.js").read_text(encoding="utf-8")
    title = (PS2 / "pacote" / "title.cfg").read_text(encoding="utf-8")
    if "const TITLE_CFG = '" + title.replace("\n", "\\n") + "'" not in nucleo:
        raise SystemExit("o TITLE_CFG do nucleo.js é diferente de pacote/title.cfg")
    if "</script" in nucleo.lower() or "@@" in nucleo:
        raise SystemExit("nucleo.js não pode ter </script nem @@")
    html = (CRIADOR / "modelo.html").read_text(encoding="utf-8")
    trocas = {
        "/*@@NUCLEO@@*/": nucleo,
        "@@ELF_B64@@": b64,
        "@@ELF_SHA_CURTO@@": sha[:16],
        "@@VERSAO@@": versao,
        "@@LEIAME_JSON@@": leia,
    }
    for marca, valor in trocas.items():
        if marca not in html:
            raise SystemExit(f"marca {marca} não encontrada no modelo")
        html = html.replace(marca, valor)
    if "@@" in html:
        raise SystemExit("sobrou marca sem trocar")
    saida.parent.mkdir(parents=True, exist_ok=True)
    saida.write_text(html, encoding="utf-8", newline="\n")
    print(f"{saida} ({saida.stat().st_size // 1024} KB, port {versao}, ELF {sha[:16]})")


if __name__ == "__main__":
    main()
