#!/usr/bin/env python3
"""apply_build.py - liga -DZX_TRACE_CAIXA no modo PRODUCAO do build_win.ps1.

Uso:  python apply_build.py build\\build_win.ps1
Valida em memoria; se a linha nao bater exatamente 1 vez, aborta sem alterar.
Guarda backup em build_win.ps1.caixa.bak (para desfazer depois da medicao,
basta copiar o .bak de volta).
"""
import sys, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "build_win.ps1"
raw = open(path, "rb").read().decode("utf-8")
crlf = "\r\n" in raw
txt = raw.replace("\r\n", "\n")

old = "$Opt = @('-O2', '-DNDEBUG')"
new = "$Opt = @('-O2', '-DNDEBUG', '-DZX_TRACE_CAIXA')"

c = txt.count(old)
if c != 1:
    print("ABORTADO: linha encontrada %d vez(es) (esperado 1). Nada foi alterado." % c)
    sys.exit(1)
txt = txt.replace(old, new)

if crlf:
    txt = txt.replace("\n", "\r\n")
shutil.copyfile(path, path + ".caixa.bak")
open(path, "wb").write(txt.encode("utf-8"))
print("OK: -DZX_TRACE_CAIXA ligado em %s (backup: %s.caixa.bak)" % (path, path))
