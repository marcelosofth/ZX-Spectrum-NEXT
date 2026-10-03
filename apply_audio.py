#!/usr/bin/env python3
"""apply_audio.py - corrige snd_libretro.c (audio do core libretro do ZEsarUX).

Uso:  python apply_audio.py caminho\\para\\snd_libretro.c
Valida TUDO em memoria primeiro. Se qualquer trecho nao bater exatamente
(uma vez so), aborta sem alterar nada. Guarda backup .bak antes de gravar.
"""
import sys, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "snd_libretro.c"
raw = open(path, "rb").read().decode("utf-8")
crlf = "\r\n" in raw
txt = raw.replace("\r\n", "\n")

EDITS = []

# 1) copia do buffer: o ZEsarUX entrega char (8 bits, 1 byte por canal)
EDITS.append((r'''            snd_ring[(size_t)snd_ring_write * 2]     = ((int16_t *)buffer)[i * 2];
            snd_ring[(size_t)snd_ring_write * 2 + 1] = ((int16_t *)buffer)[i * 2 + 1];
''', r'''            /* audio_buffer e char[AUDIO_BUFFER_SIZE*2]: 1 byte COM SINAL por
             * canal. Ler como int16_t juntava 2 amostras em uma e passava do
             * fim do buffer. Aqui: 8 bits -> 16 bits. */
            snd_ring[(size_t)snd_ring_write * 2]     = (int16_t)(((signed char *)buffer)[i * 2]     * 256);
            snd_ring[(size_t)snd_ring_write * 2 + 1] = (int16_t)(((signed char *)buffer)[i * 2 + 1] * 256);
'''))

# 2) declaracao da cota por quadro
EDITS.append((r'''    int    disponivel;
    size_t i;
''', r'''    int    disponivel;
    int    cota;
    size_t i;
'''))

# 3) leitura com cota por quadro (nao esvazia o ring de uma vez)
EDITS.append((r'''    if (disponivel > (int)max_frames)
        disponivel = (int)max_frames;
''', r'''    /* Latencia: se acumulou mais de 2 blocos, descarta o excesso antigo. */
    if (disponivel > 2 * AUDIO_BUFFER_SIZE)
    {
        snd_ring_read = (snd_ring_read + disponivel - 2 * AUDIO_BUFFER_SIZE)
                        % snd_ring_frames;
        disponivel = 2 * AUDIO_BUFFER_SIZE;
    }

    /* O emulador entrega 5 quadros de audio de uma vez (1560 amostras), mas
     * cada retro_run so deve consumir 1 quadro (312). Pedir 2048 e esvaziar
     * o ring de uma vez = 1 quadro com som e 4 em silencio = 10 estalos/s. */
    cota = AUDIO_BUFFER_SIZE / FRAMES_VECES_BUFFER_AUDIO;
    if (disponivel > cota)
        disponivel = cota;

    if (disponivel > (int)max_frames)
        disponivel = (int)max_frames;
'''))

# 4) sem preencher com silencio: devolve so o que e real
EDITS.append((r'''    /* Silencio para completar o bloco pedido. */
    for (; i < max_frames; i++)
    {
        out[i * 2]     = 0;
        out[i * 2 + 1] = 0;
    }

    return max_frames;
''', r'''    /* Devolve so os pares realmente lidos. O glue deve chamar
     * audio_batch_cb(out, retorno), e nao max_frames. */
    return i;
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
