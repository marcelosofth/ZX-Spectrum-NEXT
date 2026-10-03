#!/usr/bin/env python3
"""apply_recorte.py - recorta a borda do Next na saida: 1216x912 (4:3), centralizado.

Pre-requisito: apply_tela.py ja aplicado (buffer 1408x1216).
O jogo (256x192 a 4x) fica em x 192..1215, y 224..991; o recorte deixa 24 px do
Spectrum de borda nos lados e 18 em cima/embaixo (x0=96, y0=152).
O buffer continua 1408x1216; so o ponteiro/tamanho entregues ao RetroArch mudam.

Uso:  python apply_recorte.py zesarux-src\\video\\scr_libretro.c scr\\zesarux_glue.c
Valida os DOIS arquivos em memoria; se algo nao bater, aborta sem alterar nenhum.
Backups: .recorte.bak
"""
import sys, shutil

if len(sys.argv) != 3:
    print("Uso: python apply_recorte.py <scr_libretro.c> <zesarux_glue.c>")
    sys.exit(1)

CX, CY, CW, CH = 96, 152, 1216, 912

PLANO = [
    (sys.argv[1], [
        ("#define ZX_FB_HEIGHT  1216\n",
         "#define ZX_FB_HEIGHT  1216\n\n"
         "/* Recorte entregue ao RetroArch (tira a borda do Next, centraliza o jogo). */\n"
         "#define ZX_CROP_X  %d\n#define ZX_CROP_Y  %d\n#define ZX_CROP_W  %d\n#define ZX_CROP_H  %d\n" % (CX, CY, CW, CH)),
        ("    *width = (unsigned)zx_fb_width;\n"
         "    *height = (unsigned)zx_fb_height;\n"
         "    *pitch_bytes = (size_t)zx_fb_width * sizeof(uint32_t);\n"
         "\n"
         "    return zx_framebuffer;\n",
         "    /* Recorte: o pitch continua o do buffer inteiro; so o ponteiro inicial\n"
         "     * e o tamanho mudam. */\n"
         "    *width = (unsigned)ZX_CROP_W;\n"
         "    *height = (unsigned)ZX_CROP_H;\n"
         "    *pitch_bytes = (size_t)zx_fb_width * sizeof(uint32_t);\n"
         "\n"
         "    return zx_framebuffer + (size_t)ZX_CROP_Y * zx_fb_width + ZX_CROP_X;\n"),
    ]),
    (sys.argv[2], [
        ("    info->width      = 1408;", "    info->width      = %d;" % CW),
        ("    info->height     = 1216;", "    info->height     = %d;" % CH),
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
    shutil.copyfile(path, path + ".recorte.bak")
    open(path, "wb").write(txt.encode("utf-8"))
    print("OK: %s (backup: %s.recorte.bak)" % (path, path))
print("Saida recortada: %dx%d a partir de (%d,%d); max continua 1408x1216." % (CW, CH, CX, CY))
