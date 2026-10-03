#!/usr/bin/env bash
# build_linux.sh - gera next_libretro.so (core Next/ZEsarUX) para Linux/Batocera x86_64.
#
# Uso (na pasta do projeto, a que tem scr/ e zesarux-src/):
#   bash build/build_linux.sh            # (ou: bash build_linux.sh, se estiver na raiz)
#   bash build/build_linux.sh -j 4       # limita a 4 compilacoes em paralelo
#   bash build/build_linux.sh clean      # apaga obj-linux/ e o .so
#
# Requisitos: gcc, make nao e' necessario. No Debian/Ubuntu: apt install build-essential
set -euo pipefail

AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Funciona tanto em next/build/build_linux.sh quanto em next/build_linux.sh.
if [ -d "$AQUI/scr" ] && [ -d "$AQUI/zesarux-src" ]; then RAIZ="$AQUI"; else RAIZ="$(dirname "$AQUI")"; fi
cd "$RAIZ"

SRC="$RAIZ/zesarux-src"
PORT="$RAIZ/scr"
OBJ="$RAIZ/obj-linux"
SAIDA="$RAIZ/next_libretro.so"
JOBS="$(nproc 2>/dev/null || echo 2)"

if [ "${1:-}" = "clean" ]; then
    rm -rf "$OBJ" "$SAIDA"
    echo "limpo."
    exit 0
fi
if [ "${1:-}" = "-j" ] && [ -n "${2:-}" ]; then JOBS="$2"; fi

[ -d "$SRC" ] && [ -d "$PORT" ] || { echo "rode dentro da pasta do projeto (scr/ e zesarux-src/)"; exit 1; }
command -v gcc >/dev/null || { echo "gcc nao encontrado (apt install build-essential)"; exit 1; }

mkdir -p "$OBJ"

# Mesmas flags do build do Windows, sem -DMINGW (no Linux valem os caminhos
# nativos do ZEsarUX). -fPIC porque o resultado e' uma biblioteca compartilhada.
CFLAGS="-O2 -fPIC -fsigned-char -DNDEBUG -DZESARUX_LIBRETRO -w"
INCS="-I$SRC -I$SRC/audio -I$SRC/copy_interfaces -I$SRC/cores -I$SRC/cpus -I$SRC/machines -I$SRC/menu -I$SRC/snap -I$SRC/storage -I$SRC/soundchips -I$SRC/third_party -I$SRC/video -I$SRC/video_chips -I$SRC/zrcp -I$SRC/zxvision -I$PORT"

# Fontes do ZEsarUX que NAO entram: drivers de video/audio externos (o core usa
# so o scr_libretro/snd_libretro), o main do programa, e arquivos que nao sao
# unidades de compilacao independentes ou duplicam simbolos.
EXCLUIR='other_sources_not_ZEsarUX/|/main_unix\.c$|/common_sdl2?\.c$|/cursesw_ext\.c$|/realjoystick_linux\.c$|/utils_math_desactivado\.c$|/debug_nested_functions\.c$|/oldzxpand\.c$|/cpus/m68k_in\.c$|/baseconf_orig\.c$|/audio/audio(alsa|alsa_stereo|coreaudio|pulse|sdl|sdl2|onebitspeaker)\.c$|/video/(scraa|scrcaca|scrcurses|scrcurses_old_utf|scrsdl|scrsdl2|scrtemplate|scrxwindows|scrfbdev)\.c$'

mapfile -t FONTES < <(find "$SRC" -name '*.c' | sort | grep -Ev "$EXCLUIR")
FONTES+=("$PORT/zesarux_glue.c" "$PORT/libretro.c")
echo "fontes: ${#FONTES[@]}  (paralelo: $JOBS)"

compila() {
    local f="$1" o
    o="$OBJ/$(echo "${f#$RAIZ/}" | tr '/' '_').o"
    # os arquivos do port mudam toda hora: sempre recompila. Os do ZEsarUX so
    # se o .o nao existir ou o .c for mais novo.
    case "$f" in
        "$PORT"/*|*/scr_libretro.c|*/snd_libretro.c) ;;
        *) [ -f "$o" ] && [ ! "$f" -nt "$o" ] && return 0 ;;
    esac
    if ! gcc $CFLAGS $INCS -c "$f" -o "$o" 2>"$o.err"; then
        echo "FALHOU: $f" >&2; cat "$o.err" >&2; return 255
    fi
}
export -f compila
export RAIZ PORT OBJ CFLAGS INCS

printf '%s\0' "${FONTES[@]}" | xargs -0 -n1 -P "$JOBS" bash -c 'compila "$0"'

echo "linkando..."
gcc -shared -o "$SAIDA" "$OBJ"/*.o -lm -lpthread -ldl -Wl,--no-undefined
strip --strip-unneeded "$SAIDA" 2>/dev/null || true
echo "ok: $SAIDA ($(du -h "$SAIDA" | cut -f1))"
