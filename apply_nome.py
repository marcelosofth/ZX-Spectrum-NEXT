#!/usr/bin/env python3
"""apply_nome.py - renomeia o core para NEXT.

  libretro.c : CORE_NAME "ZEsarUX" -> "NEXT"  e  CORE_VERSION "0.1-dev" -> ""
               (rodape do menu rapido deixa de mostrar "ZEsarUX (0.1-dev)")
  .info      : display_name -> "ZX Spectrum Next" e description atualizada
               (lista "Carregar nucleo")

Uso:  python apply_nome.py scr\\libretro.c F:\\Emulators\\RetroArch\\info\\zesarux_libretro.info
Valida os DOIS arquivos em memoria; se qualquer trecho nao bater exatamente uma
vez, aborta sem alterar nenhum. Backups: .nome.bak
"""
import sys, shutil

if len(sys.argv) != 3:
    print("Uso: python apply_nome.py <libretro.c> <zesarux_libretro.info>")
    sys.exit(1)

PLANO = [
    (sys.argv[1], [
        ('#define CORE_NAME      "ZEsarUX"', '#define CORE_NAME      "NEXT"'),
        ('#define CORE_VERSION   "0.1-dev"', '#define CORE_VERSION   ""'),
    ]),
    (sys.argv[2], [
        ('display_name = "ZX Spectrum Next (ZEsarUX - EXPERIMENTAL)"',
         'display_name = "ZX Spectrum Next"'),
        ('description = "ZX Spectrum / Next via ZEsarUX. EXPERIMENTAL: este nucleo esta em desenvolvimento e mostra apenas um padrao de teste, nao emula nada ainda."',
         'description = "ZX Spectrum Next via ZEsarUX."'),
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
    shutil.copyfile(path, path + ".nome.bak")
    open(path, "wb").write(txt.encode("utf-8"))
    print("OK: %s (backup: %s.nome.bak)" % (path, path))
print("Recompile o core para o rodape do menu rapido mudar (o .info vale ao reabrir o RetroArch).")
