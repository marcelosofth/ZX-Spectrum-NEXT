#!/usr/bin/env python3
"""
apply_next_ext.py

Adiciona a extensao "next" na lista de extensoes validas do core
(retro_get_system_info -> valid_extensions em scr/libretro.c), para o
RetroArch aceitar o arquivo "falso" Next.next e lancar o Next vazio.

Uso: python apply_next_ext.py [caminho\\libretro.c]
Sem argumento, usa ./scr/libretro.c

Valida tudo em memoria. Se algo nao bater, aborta sem alterar nada.
Antes de gravar, cria libretro.c.bak_next_ext.
"""
import sys, os, shutil

def abortar(msg):
    print("ABORTADO (nada foi alterado): " + msg)
    sys.exit(1)

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join("scr", "libretro.c")
if not os.path.isfile(path):
    abortar("arquivo nao encontrado: " + path)
print("Arquivo: " + path)

with open(path, "rb") as f:
    dados = f.read()

antigo = b'info->valid_extensions = "nex|tap|tzx|sna|z80|szx|trd|dsk|scr|p|o|rom|bin|vhd";'
novo   = b'info->valid_extensions = "next|nex|tap|tzx|sna|z80|szx|trd|dsk|scr|p|o|rom|bin|vhd";'

if novo in dados or b'"next|' in dados:
    abortar("a extensao next ja esta na lista; nada a fazer.")
if dados.count(antigo) != 1:
    abortar("linha valid_extensions esperada nao encontrada exatamente 1 vez (achei %d). "
            "O arquivo e' o certo?" % dados.count(antigo))

resultado = dados.replace(antigo, novo, 1)
if resultado.count(novo) != 1 or len(resultado) != len(dados) + 5:
    abortar("verificacao interna falhou apos a edicao.")

shutil.copyfile(path, path + ".bak_next_ext")
with open(path, "wb") as f:
    f.write(resultado)

print("OK: 'next' adicionado em valid_extensions.")
print("Backup: " + path + ".bak_next_ext")
