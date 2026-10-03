#!/usr/bin/env python3
"""apply_caixa.py - conserta a medicao ZX_TRACE_CAIXA em scr_libretro.c.

O recorte (return) acontecia ANTES da medicao, entao a caixa nunca passava de
1279x959 e nao podia mostrar pixels fora do buffer. Esta correcao mede antes do
recorte e conta quantos pixels foram cortados.

Uso:  python apply_caixa.py caminho\\para\\scr_libretro.c
Valida tudo em memoria; se algo nao bater, aborta sem alterar nada.
"""
import sys, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "scr_libretro.c"
raw = open(path, "rb").read().decode("utf-8")
crlf = "\r\n" in raw
txt = raw.replace("\r\n", "\n")

EDITS = []

EDITS.append((r'''static long zx_box_pixels;
''', r'''static long zx_box_pixels;
static long zx_box_cortados;
'''))

EDITS.append((r'''    zx_box_pixels = 0;
}''', r'''    zx_box_pixels = 0;
    zx_box_cortados = 0;
}'''))

EDITS.append((r'''            zx_box_pixels, zx_fb_width, zx_fb_height);
}''', r'''            zx_box_pixels, zx_fb_width, zx_fb_height);
    fprintf(stderr, "[caixa] pixels fora do buffer (cortados): %ld\n", zx_box_cortados);
}'''))

# tira o recorte de antes da medicao
EDITS.append((r'''    if (x < 0 || y < 0 || x >= zx_fb_width || y >= zx_fb_height)
        return;

#ifdef ZX_TRACE_CAIXA
''', r'''#ifdef ZX_TRACE_CAIXA
'''))

# recorte passa a vir depois da medicao
EDITS.append((r'''    zx_box_pixels++;
#endif
''', r'''    zx_box_pixels++;
    if (x < 0 || y < 0 || x >= zx_fb_width || y >= zx_fb_height)
        zx_box_cortados++;
#endif

    if (x < 0 || y < 0 || x >= zx_fb_width || y >= zx_fb_height)
        return;
'''))

for n, (old, new) in enumerate(EDITS, 1):
    c = txt.count(old)
    if c != 1:
        print("ABORTADO: trecho %d encontrado %d vez(es) (esperado 1). Nada foi alterado." % (n, c))
        sys.exit(1)
    txt = txt.replace(old, new)

if crlf:
    txt = txt.replace("\n", "\r\n")
shutil.copyfile(path, path + ".bak")
open(path, "wb").write(txt.encode("utf-8"))
print("OK: %d trechos aplicados em %s (backup: %s.bak)" % (len(EDITS), path, path))
