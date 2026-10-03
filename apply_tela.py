#!/usr/bin/env python3
"""apply_tela.py - aumenta o framebuffer e o av_info de 1280x960 para 1408x1216.

Medido (ZX_TRACE_CAIXA): o Next desenha 1408x1216 e o buffer de 1280x960
cortava 128 px a direita e 256 embaixo.

Uso:  python apply_tela.py zesarux-src\\video\\scr_libretro.c scr\\zesarux_glue.c
Valida os DOIS arquivos em memoria; se qualquer trecho nao bater exatamente
uma vez, aborta sem alterar nenhum. Backups: .tela.bak
"""
import sys, shutil

if len(sys.argv) != 3:
    print("Uso: python apply_tela.py <scr_libretro.c> <zesarux_glue.c>")
    sys.exit(1)

W, H = 1408, 1216

PLANO = [
    (sys.argv[1], [
        ("#define ZX_FB_WIDTH   1280", "#define ZX_FB_WIDTH   %d" % W),
        ("#define ZX_FB_HEIGHT  960",  "#define ZX_FB_HEIGHT  %d" % H),
    ]),
    (sys.argv[2], [
        ("    info->width      = 1280;", "    info->width      = %d;" % W),
        ("    info->height     = 960;",  "    info->height     = %d;" % H),
        ("    info->max_width  = 1280;", "    info->max_width  = %d;" % W),
        ("    info->max_height = 960;",  "    info->max_height = %d;" % H),
    ]),
]

novos = []
for path, edits in PLANO:
    raw = open(path, "rb").read().decode("utf-8")
    crlf = "\r\n" in raw
    txt = raw.replace("\r\n", "\n")
    for n, (old, new) in enumerate(edits, 1):
        c = txt.count(old)
        if c != 1:
            print("ABORTADO: %s, trecho %d encontrado %d vez(es) (esperado 1). Nada foi alterado." % (path, n, c))
            sys.exit(1)
        txt = txt.replace(old, new)
    if crlf:
        txt = txt.replace("\n", "\r\n")
    novos.append((path, txt))

for path, txt in novos:
    shutil.copyfile(path, path + ".tela.bak")
    open(path, "wb").write(txt.encode("utf-8"))
    print("OK: %s (backup: %s.tela.bak)" % (path, path))
print("Buffer e av_info agora em %dx%d." % (W, H))
