#!/usr/bin/env python3
"""apply_analogico.py - analogico esquerdo passa a mover como o direcional digital.

Pre-requisito: apply_zoom.py ja aplicado (o trecho e inserido antes do bloco do Select).
Zona morta de ~50% (16000 de 32767), para nao disparar com o analogico em repouso.

Uso:  python apply_analogico.py scr\\libretro.c
Valida em memoria; se o trecho nao bater exatamente uma vez, aborta sem alterar. Backup: .analogico.bak
"""
import sys, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "libretro.c"
raw = open(path, "rb").read().decode("utf-8")
crlf = "\r\n" in raw
txt = raw.replace("\r\n", "\n")

old = ("      if (port == 0)\n"
       "      {\n"
       "         /* Select: avanca o zoom 1x/2x/3x (so na borda de subida). */\n")
new = ("      {\n"
       "         /* Analogico esquerdo -> direcional digital (zona morta ~50%). */\n"
       "         int ax = input_state_cb(port, RETRO_DEVICE_ANALOG,\n"
       "                     RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);\n"
       "         int ay = input_state_cb(port, RETRO_DEVICE_ANALOG,\n"
       "                     RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);\n"
       "         if (ax < -16000)\n"
       "            mask |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_LEFT);\n"
       "         else if (ax > 16000)\n"
       "            mask |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);\n"
       "         if (ay < -16000)\n"
       "            mask |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_UP);\n"
       "         else if (ay > 16000)\n"
       "            mask |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_DOWN);\n"
       "      }\n"
       + old)

c = txt.count(old)
if c != 1:
    print("ABORTADO: trecho encontrado %d vez(es) (esperado 1; o apply_zoom.py ja foi aplicado?). Nada foi alterado." % c)
    sys.exit(1)
txt = txt.replace(old, new)

if crlf:
    txt = txt.replace("\n", "\r\n")
shutil.copyfile(path, path + ".analogico.bak")
open(path, "wb").write(txt.encode("utf-8"))
print("OK: analogico esquerdo ligado em %s (backup: %s.analogico.bak)" % (path, path))
