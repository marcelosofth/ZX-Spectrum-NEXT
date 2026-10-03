/*
 * zesarux_glue.h
 *
 * Camada fina entre libretro.c e o codigo do ZEsarUX.
 *
 * libretro.c SO conhece estas funcoes. Para o port real, implemente-as em
 * zesarux_glue.c chamando o nucleo do ZEsarUX (CPU, ULA, TBBlue/Next, audio).
 * Enquanto isso, zesarux_glue_stub.c fornece uma versao de teste (padrao de
 * cores + audio mudo) para voce validar o lado libretro no RetroArch.
 *
 * Ideias para a implementacao real (confirme os nomes no codigo do ZEsarUX):
 *   zx_init           -> inicializar o emulador sem interface: drivers nulos
 *                        de video/audio, sem ZXVision, sem threads de UI,
 *                        maquina = ZX Spectrum Next.
 *   zx_run_frame      -> executar o loop de CPU ate completar UM frame de
 *                        video (em vez do loop infinito do main).
 *   zx_get_framebuffer-> converter/expor o buffer de video interno em XRGB8888.
 *   zx_get_audio      -> ler o buffer de audio (beeper + AY + DACs) e entregar
 *                        em int16 estereo intercalado.
 *   zx_set_key        -> acionar a matriz de teclado do Spectrum.
 *   zx_set_joy        -> mapear para Kempston/Sinclair/Cursor.
 *   zx_state_*        -> adaptar o sistema de snapshots para memoria.
 */
#ifndef ZESARUX_GLUE_H
#define ZESARUX_GLUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct
{
   unsigned width;        /* largura atual da imagem */
   unsigned height;       /* altura atual da imagem */
   unsigned max_width;    /* maior largura possivel */
   unsigned max_height;   /* maior altura possivel */
   double   fps;          /* 50.0 ou 60.0 */
   double   sample_rate;  /* ex.: 44100.0 */
   float    aspect;       /* 0 = usar width/height */
} zx_av_info;

/* Ciclo de vida */
bool zx_init(const char *system_dir, const char *save_dir);
void zx_deinit(void);

/* path == NULL significa iniciar sem conteudo (boot do NextZXOS/BASIC) */
bool zx_load_content(const char *path);
void zx_unload_content(void);
void zx_reset(bool hard);

/* O SD (tbblue.mmc) foi encontrado?
 *
 * Sem ele o NextZXOS nao sobe: o boot fica em uma tela vazia e nenhum .nex
 * abre. O .mmc tem 1,05 GB e nao e' nosso para distribuir, entao o usuario
 * precisa trazer o dele -- e o core precisa saber que ele falta para
 * avisar em vez de fingir que esta tudo bem.
 *
 * O libretro.c usa isto em retro_load_game() para mandar um
 * RETRO_ENVIRONMENT_SET_MESSAGE, que aparece na tela do frontend. */
bool zx_has_sd(void);

/* Pausa. O ZEsarUX tem laco proprio de emulacao e o frontend nao: sem isto
 * nao ha como parar a CPU, e o Next continua rodando enquanto o menu abre --
 * o que torna impossivel carregar um .nex com a maquina travada. */
void zx_set_paused(bool paused);
bool zx_is_paused(void);

/* Emula exatamente um frame de video */
void zx_run_frame(void);

/* Video: devolve ponteiro para pixels XRGB8888 */
const uint32_t *zx_get_framebuffer(unsigned *width, unsigned *height, size_t *pitch_bytes);

/* Audio: escreve ate max_frames frames estereo int16; devolve quantos escreveu */
size_t zx_get_audio(int16_t *out, size_t max_frames);

/* Entrada */
void zx_set_key(unsigned retro_keycode, bool down);
void zx_set_joy(unsigned port, uint16_t retro_joypad_mask); /* bit i = RETRO_DEVICE_ID_JOYPAD_i */

/* Opcoes do core (chaves definidas em libretro.c) */
void zx_set_option(const char *key, const char *value);

/* Informacoes de A/V (chamar depois de zx_init) */
void zx_get_av_info(zx_av_info *info);

/* Save states. zx_state_size() == 0 desativa o recurso. */
size_t zx_state_size(void);
bool   zx_state_save(void *data, size_t size);
bool   zx_state_load(const void *data, size_t size);

#endif
