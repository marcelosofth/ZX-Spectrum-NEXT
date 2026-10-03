#!/usr/bin/env python3
"""apply_zoom.py - botao Select cicla o zoom 1x -> 2x -> 3x -> 1x (todos 4:3, com borda).

Pre-requisito: apply_tela.py e apply_recorte.py ja aplicados.
Niveis (recorte dentro do buffer 1408x1216; o jogo 256x192 fica em x192,y224):
  1x 1408x1056 em (  0, 80)  borda 48/36 px do Spectrum
  2x 1216x912  em ( 96,152)  borda 24/18
  3x 1088x816  em (160,200)  borda  8/ 6
O retro_run ja faz SET_GEOMETRY quando w/h mudam, entao basta trocar o recorte.

Uso:  python apply_zoom.py zesarux-src\\video\\scr_libretro.c scr\\zesarux_glue.c scr\\libretro.c
Valida os TRES arquivos em memoria; se qualquer trecho nao bater exatamente uma
vez, aborta sem alterar nenhum. Backups: .zoom.bak
"""
import sys, shutil

if len(sys.argv) != 4:
    print("Uso: python apply_zoom.py <scr_libretro.c> <zesarux_glue.c> <libretro.c>")
    sys.exit(1)

PLANO = [
    (sys.argv[1], [
        ("/* Recorte entregue ao RetroArch (tira a borda do Next, centraliza o jogo). */\n"
         "#define ZX_CROP_X  96\n#define ZX_CROP_Y  152\n#define ZX_CROP_W  1216\n#define ZX_CROP_H  912\n",
         "/* Zoom: tres recortes 4:3 centralizados no jogo, todos com borda.\n"
         " * O botao Select (ver libretro.c) chama zx_libretro_zoom_next(). */\n"
         "static const struct { int x, y, w, h; } zx_zoom_tab[3] = {\n"
         "    {   0,  80, 1408, 1056 },   /* 1x */\n"
         "    {  96, 152, 1216,  912 },   /* 2x */\n"
         "    { 160, 200, 1088,  816 },   /* 3x */\n"
         "};\n"
         "static int zx_zoom_nivel = 0;\n\n"
         "void zx_libretro_zoom_next(void)\n"
         "{\n"
         "    zx_zoom_nivel = (zx_zoom_nivel + 1) % 3;\n"
         "}\n"),
        ("    *width = (unsigned)ZX_CROP_W;\n    *height = (unsigned)ZX_CROP_H;\n",
         "    *width = (unsigned)zx_zoom_tab[zx_zoom_nivel].w;\n"
         "    *height = (unsigned)zx_zoom_tab[zx_zoom_nivel].h;\n"),
        ("    return zx_framebuffer + (size_t)ZX_CROP_Y * zx_fb_width + ZX_CROP_X;\n",
         "    return zx_framebuffer + (size_t)zx_zoom_tab[zx_zoom_nivel].y * zx_fb_width\n"
         "                          + zx_zoom_tab[zx_zoom_nivel].x;\n"),
    ]),
    (sys.argv[2], [
        ("    info->width      = 1216;", "    info->width      = 1408;"),
        ("    info->height     = 912;",  "    info->height     = 1056;"),
    ]),
    (sys.argv[3], [
        ("void retro_run(void)\n{\n",
         "extern void zx_libretro_zoom_next(void);\n\nvoid retro_run(void)\n{\n"),
        ("      zx_set_joy(port, mask);\n",
         "      if (port == 0)\n"
         "      {\n"
         "         /* Select: avanca o zoom 1x/2x/3x (so na borda de subida). */\n"
         "         static int sel_prev;\n"
         "         int sel = (mask >> RETRO_DEVICE_ID_JOYPAD_SELECT) & 1;\n"
         "         if (sel && !sel_prev)\n"
         "            zx_libretro_zoom_next();\n"
         "         sel_prev = sel;\n"
         "      }\n"
         "      zx_set_joy(port, mask);\n"),
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
    shutil.copyfile(path, path + ".zoom.bak")
    open(path, "wb").write(txt.encode("utf-8"))
    print("OK: %s (backup: %s.zoom.bak)" % (path, path))
print("Select agora cicla o zoom 1x -> 2x -> 3x -> 1x.")
