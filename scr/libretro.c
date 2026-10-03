/*
 * libretro.c - core libretro para o ZEsarUX (ZX Spectrum Next)
 *
 * Licenca: GPLv3 (mesma do ZEsarUX).
 *
 * Este arquivo implementa a API libretro e delega tudo o que e especifico do
 * emulador para zesarux_glue.h.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "zesarux_glue.h"

#define CORE_NAME      "NEXT"
#define CORE_VERSION   "13.0"  /* versao do ZEsarUX em que o port se baseia
                                   * (EMULATOR_VERSION do cpu.h). Aparece em
                                   * "Core Info" e no rodape do menu do
                                   * RetroArch, depois do nome do core. */
#define AUDIO_MAX_FRAMES 2048

/* ------------------------------------------------------------------------ */
/* Callbacks do frontend                                                    */
/* ------------------------------------------------------------------------ */
static retro_environment_t        environ_cb;
static retro_video_refresh_t      video_cb;
static retro_audio_sample_t       audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t         input_poll_cb;
static retro_input_state_t        input_state_cb;
static retro_log_printf_t         log_cb;

/* ------------------------------------------------------------------------ */
/* Estado do core                                                           */
/* ------------------------------------------------------------------------ */
static char     system_dir[4096];
static char     save_dir[4096];
static bool     core_initialized;
static bool     content_loaded;
static unsigned cur_width, cur_height;
static double   cur_fps;
static int16_t  audio_buf[AUDIO_MAX_FRAMES * 2];

/* Quantas amostras de audio sao devidas a UM quadro de video. Recalculado
 * sempre que o av_info muda, porque depende da taxa e do fps -- e os dois
 * podem mudar com o jogo (ver retro_run e a nota em retro_run). */
static size_t   audio_frames_por_quadro = 312;   /* 15600 Hz / 50 fps */

static void recalcula_audio_por_quadro(const zx_av_info *info)
{
   double por_quadro;

   if (!info || info->fps <= 0.0 || info->sample_rate <= 0.0)
      return;

   por_quadro = (double)info->sample_rate / info->fps;

   /* Arredonda, mas nunca a zero: devolver 0 faz o frontend repetir o
    * ultimo bloco e o audio engasga. E nunca acima do buffer. */
   audio_frames_por_quadro = (size_t)(por_quadro + 0.5);
   if (audio_frames_por_quadro == 0)
      audio_frames_por_quadro = 1;
   if (audio_frames_por_quadro > AUDIO_MAX_FRAMES)
      audio_frames_por_quadro = AUDIO_MAX_FRAMES;
}

/* ------------------------------------------------------------------------ */
/* Log                                                                      */
/* ------------------------------------------------------------------------ */
static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
   va_list va;
   (void)level;
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}

/* ------------------------------------------------------------------------ */
/* Core options                                                             */
/* ------------------------------------------------------------------------ */
static const char *OPT_CPU_SPEED = "zesarux_cpu_speed";
static const char *OPT_REFRESH   = "zesarux_refresh";

/* Formato antigo (v0): so e' usado se o frontend nao entender o v2. */
static struct retro_variable core_vars[] = {
   { "zesarux_cpu_speed", "CPU speed; 3.5MHz|7MHz|14MHz|28MHz" },
   { "zesarux_refresh",   "Refresh rate; 50 Hz|60 Hz" },
   { NULL, NULL }
};

/* Formato v2: permite texto de ajuda (info) e submenus (categorias).
 * O tutorial de instalacao e' uma categoria: cada passo e' uma opcao com um
 * unico valor, e o texto do passo aparece como descricao da entrada.
 * Para editar o tutorial, mude so os campos "info". */
static struct retro_core_option_v2_category core_cats[] = {
   { "tutorial", "Installation tutorial", NULL },
   { NULL, NULL, NULL }
};

#define TUT_VALORES { { "read", "Read" }, { NULL, NULL } }

static struct retro_core_option_v2_definition core_defs[] = {
   { "zesarux_cpu_speed", "CPU speed", NULL, NULL, NULL, NULL,
     { { "3.5MHz", NULL }, { "7MHz", NULL }, { "14MHz", NULL },
       { "28MHz", NULL }, { NULL, NULL } },
     "3.5MHz" },
   { "zesarux_refresh", "Refresh rate", NULL, NULL, NULL, NULL,
     { { "50 Hz", NULL }, { "60 Hz", NULL }, { NULL, NULL } },
     "50 Hz" },

   { "zesarux_tut_1", "1. Required files", NULL,
     "Place tbblue_loader.rom and tbblue.mmc in /userdata/bios/zesarux/.",
     NULL, "tutorial", TUT_VALORES, "read" },
   { "zesarux_tut_2", "2. File SD (tbblue.mmc)", NULL,
     "This is the Next SD card image with NextZXOS. It does not include the "
     "core, and without it, NextZXOS will not boot.",
     NULL, "tutorial", TUT_VALORES, "read" },
   { "zesarux_tut_3", "3. Start a game", NULL,
     "Load a file (.nex, .tap, .tzx, .sna, .z80, .szx, .trd, .dsk, .scr, "
     ".vhd...) or start the core without content to boot directly into "
     "NextZXOS.",
     NULL, "tutorial", TUT_VALORES, "read" },
   { "zesarux_tut_4", "4. Joystick/Controls", NULL,
     "Directional: D-Pad or Analog Stick. Buttons: B, A, Y, X. R1: Virtual "
     "keyboard. L1: Zoom 1x, 2x, 3x. Select: Reset. Also, use the "
     "Batocera/Retroarch keyboard-to-joystick mapping!",
     NULL, "tutorial", TUT_VALORES, "read" },
   { "zesarux_tut_5", "5. Run Next", NULL,
     "Create an empty file named zxnext.next and place it in /userdata/roms/next, "
     "then refresh the game list!",
     NULL, "tutorial", TUT_VALORES, "read" },

   { NULL, NULL, NULL, NULL, NULL, NULL, { { NULL, NULL } }, NULL }
};

static struct retro_core_options_v2 core_options_v2 = { core_cats, core_defs };

static void update_av_info(void)
{
   struct retro_system_av_info av;
   zx_av_info info;

   if (!core_initialized)
      return;

   zx_get_av_info(&info);
   memset(&av, 0, sizeof(av));
   av.geometry.base_width   = info.width;
   av.geometry.base_height  = info.height;
   av.geometry.max_width    = info.max_width;
   av.geometry.max_height   = info.max_height;
   av.geometry.aspect_ratio = info.aspect;
   av.timing.fps            = info.fps;
   av.timing.sample_rate    = info.sample_rate;

   cur_fps = info.fps;
   recalcula_audio_por_quadro(&info);
   environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
}

static void check_variables(bool notify_av)
{
   struct retro_variable var;
   bool refresh_changed = false;

   var.key = OPT_CPU_SPEED;
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      zx_set_option(OPT_CPU_SPEED, var.value);

   var.key = OPT_REFRESH;
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      zx_set_option(OPT_REFRESH, var.value);
      if (core_initialized)
      {
         zx_av_info info;
         zx_get_av_info(&info);
         if (info.fps != cur_fps)
            refresh_changed = true;
      }
   }

   if (notify_av && refresh_changed)
      update_av_info();
}

/* ------------------------------------------------------------------------ */
/* Teclado                                                                  */
/* ------------------------------------------------------------------------ */
static void keyboard_event(bool down, unsigned keycode,
                           uint32_t character, uint16_t key_modifiers)
{
   (void)character;
   (void)key_modifiers;
   zx_set_key(keycode, down);
}

/* ------------------------------------------------------------------------ */
/* Setters de callbacks                                                     */
/* ------------------------------------------------------------------------ */
void retro_set_environment(retro_environment_t cb)
{
   struct retro_log_callback logging;
   bool no_game = true;

   environ_cb = cb;

   /* O Next pode iniciar sem conteudo (NextZXOS / BASIC) */
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
   {
      unsigned opt_version = 0;
      if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &opt_version) &&
          opt_version >= 2)
         cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &core_options_v2);
      else
         cb(RETRO_ENVIRONMENT_SET_VARIABLES, core_vars);
   }

   if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
      log_cb = logging.log;
   else
      log_cb = fallback_log;
}

void retro_set_video_refresh(retro_video_refresh_t cb)           { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)             { audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb)                 { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb)               { input_state_cb = cb; }

/* ------------------------------------------------------------------------ */
/* Ciclo de vida                                                            */
/* ------------------------------------------------------------------------ */
unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_init(void)
{
   const char *dir = NULL;
   struct retro_keyboard_callback kb = { keyboard_event };

   system_dir[0] = '\0';
   save_dir[0]   = '\0';

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
      snprintf(system_dir, sizeof(system_dir), "%s", dir);
   dir = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
      snprintf(save_dir, sizeof(save_dir), "%s", dir);

   environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb);

   core_initialized = zx_init(system_dir, save_dir);
   if (!core_initialized)
      log_cb(RETRO_LOG_ERROR, "[ZEsarUX] falha em zx_init()\n");
}

void retro_deinit(void)
{
   if (core_initialized)
      zx_deinit();
   core_initialized = false;
   content_loaded   = false;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = CORE_NAME;
   info->library_version  = CORE_VERSION;
   /* O ZEsarUX abre os arquivos por caminho */
   info->need_fullpath    = true;
   info->block_extract    = false;
   info->valid_extensions = "next|nex|tap|tzx|sna|z80|szx|trd|dsk|scr|p|o|rom|bin|vhd";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   zx_av_info av;

   memset(&av, 0, sizeof(av));
   if (core_initialized)
      zx_get_av_info(&av);
   else
   {
      av.width = 320; av.height = 256;
      av.max_width = 720; av.max_height = 576;
      av.fps = 50.0; av.sample_rate = 44100.0;
   }

   memset(info, 0, sizeof(*info));
   info->geometry.base_width   = av.width;
   info->geometry.base_height  = av.height;
   info->geometry.max_width    = av.max_width;
   info->geometry.max_height   = av.max_height;
   info->geometry.aspect_ratio = av.aspect;
   info->timing.fps            = av.fps;
   info->timing.sample_rate    = av.sample_rate;

   cur_width  = av.width;
   cur_height = av.height;
   cur_fps    = av.fps;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

bool retro_load_game(const struct retro_game_info *game)
{
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

   if (!core_initialized)
      return false;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      log_cb(RETRO_LOG_ERROR, "[ZEsarUX] XRGB8888 nao suportado pelo frontend\n");
      return false;
   }

   check_variables(false);

   /* game pode ser NULL (no-game) */
   if (!zx_load_content(game ? game->path : NULL))
   {
      log_cb(RETRO_LOG_ERROR, "[ZEsarUX] nao foi possivel carregar: %s\n",
             (game && game->path) ? game->path : "(nenhum)");
      return false;
   }

   /* O Next nao sobe sem o cartao SD (tbblue.mmc, ~1 GB, nao distribuido).
    * Antes disto o core so avisava no log e seguia: o usuario via tela
    * vazia sem saber por que. Agora avisa tambem na tela, o quanto o
    * frontend permitir. Nao recusamos o carregamento aqui porque o core
    * tambem aceita conteudo que nao depende do Next (ex.: .tap/.z80 em
    * maquina classica); quem decide se o SD e obrigatorio para o conteudo
    * atual e' zx_load_content/set_machine, nao este arquivo. */
   if (!zx_has_sd())
   {
      struct retro_message msg;
      log_cb(RETRO_LOG_ERROR,
             "[ZEsarUX] tbblue.mmc nao encontrado em system_dir/zesarux/. "
             "O NextZXOS nao vai subir sem ele.\n");

      msg.msg    = "ZEsarUX: tbblue.mmc ausente -- coloque-o em system_dir/zesarux/";
      msg.frames = 600; /* ~12s a 50 Hz; so uma dica, nao bloqueia nada */
      environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
   }

   content_loaded = true;
   return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type; (void)info; (void)num;
   return false;
}

void retro_unload_game(void)
{
   if (content_loaded)
      zx_unload_content();
   content_loaded = false;
}

void retro_reset(void)
{
   if (core_initialized)
      zx_reset(false);
}

unsigned retro_get_region(void)
{
   return (cur_fps > 55.0) ? RETRO_REGION_NTSC : RETRO_REGION_PAL;
}

/* ------------------------------------------------------------------------ */
/* Loop principal: UM frame por chamada                                     */
/* ------------------------------------------------------------------------ */
extern void zx_libretro_zoom_next(void);

/* Teclado virtual (video/scr_libretro.c) */
extern void zx_libretro_osk_toggle(void);
extern void zx_libretro_osk_close(void);
extern int  zx_libretro_osk_active(void);
extern void zx_libretro_osk_move(int dir);   /* 0 cima, 1 baixo, 2 esq, 3 dir */
extern void zx_libretro_osk_select(void);
extern void zx_libretro_osk_tick(void);

void retro_run(void)
{
   const uint32_t *fb;
   unsigned w, h, port, id;
   size_t pitch, frames;
   bool updated = false;

#ifdef ZX_TRACE
   /* Medir retro_run POR PARTES.
    *
    * zx_run_frame() sozinho mede ~12 ms, mas o harness media 20,8 ms por
    * iteracao. Ou seja: ~8,7 ms de cada quadro acontecem FORA da emulacao.
    * Sem esta divisao, qualquer conclusao sobre "o gargalo" e' sobre a emulacao
    * e ignora o resto -- que e' exatamente onde estao video_cb, o envio ao
    * frontend e o audio. */
   LARGE_INTEGER q0, q1, q2, q3, q4, fq;
   QueryPerformanceFrequency(&fq);
   QueryPerformanceCounter(&q0);
#endif

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables(true);

   input_poll_cb();

   for (port = 0; port < 2; port++)
   {
      uint16_t mask = 0;
      for (id = 0; id < 16; id++)
         if (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, id))
            mask |= (uint16_t)(1u << id);
      {
         /* Analogico esquerdo -> direcional digital (zona morta ~50%). */
         int ax = input_state_cb(port, RETRO_DEVICE_ANALOG,
                     RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
         int ay = input_state_cb(port, RETRO_DEVICE_ANALOG,
                     RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
         if (ax < -16000)
            mask |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
         else if (ax > 16000)
            mask |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
         if (ay < -16000)
            mask |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_UP);
         else if (ay > 16000)
            mask |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
      }
      if (port == 0)
      {
         static int l1_prev;
         static int sel_prev;
         int l1    = (mask >> RETRO_DEVICE_ID_JOYPAD_L) & 1;
         int sel   = (mask >> RETRO_DEVICE_ID_JOYPAD_SELECT) & 1;

         /* L1: avanca o zoom 1x/2x/3x (so na borda de subida). */
         if (l1 && !l1_prev)
            zx_libretro_zoom_next();
         l1_prev = l1;

         /* Select: reset (so na borda de subida, para nao resetar a cada
          * quadro enquanto o botao estiver pressionado). Usa o mesmo reset
          * suave do retro_reset(). */
         if (sel && !sel_prev)
            zx_reset(false);
         sel_prev = sel;
      }
      if (port == 0)
      {
         /* Teclado virtual (o F8 do ZEsarUX).
          *
          *   R1 (RETRO_DEVICE_ID_JOYPAD_R)  abre / fecha
          *   direcional / analogico         move o cursor
          *   A                              escolhe a tecla (Enter)
          *   B                              fecha (ESC)
          *
          * Com o teclado aberto o jogo nao recebe o joypad, e os botoes
          * usados aqui ficam bloqueados ate serem soltos, para o B que fechou
          * nao virar um tiro no jogo logo em seguida. */
         static const int dir_id[4] = {
            RETRO_DEVICE_ID_JOYPAD_UP,   RETRO_DEVICE_ID_JOYPAD_DOWN,
            RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT };
         static int      r_prev, a_prev, b_prev, dir_rep[4];
         static uint16_t dir_prev, osk_block;
         const uint16_t  osk_bits =
            (uint16_t)((1u << RETRO_DEVICE_ID_JOYPAD_UP)   | (1u << RETRO_DEVICE_ID_JOYPAD_DOWN) |
                       (1u << RETRO_DEVICE_ID_JOYPAD_LEFT) | (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT) |
                       (1u << RETRO_DEVICE_ID_JOYPAD_A)    | (1u << RETRO_DEVICE_ID_JOYPAD_B) |
                       (1u << RETRO_DEVICE_ID_JOYPAD_R));
         int r = (mask >> RETRO_DEVICE_ID_JOYPAD_R) & 1;
         int a = (mask >> RETRO_DEVICE_ID_JOYPAD_A) & 1;
         int b = (mask >> RETRO_DEVICE_ID_JOYPAD_B) & 1;
         int was_on = zx_libretro_osk_active();
         int i;

         if (r && !r_prev && !zx_is_paused())
            zx_libretro_osk_toggle();

         if (!was_on && zx_libretro_osk_active())
            for (i = 0; i < 4; i++)
               dir_rep[i] = 18;     /* recem aberto: nao repete de imediato */

         if (was_on && zx_libretro_osk_active())
         {
            for (i = 0; i < 4; i++)
            {
               uint16_t bit = (uint16_t)(1u << dir_id[i]);
               if (mask & bit)
               {
                  if (!(dir_prev & bit))
                  {
                     zx_libretro_osk_move(i);
                     dir_rep[i] = 18;            /* espera antes de repetir */
                  }
                  else if (--dir_rep[i] <= 0)
                  {
                     zx_libretro_osk_move(i);
                     dir_rep[i] = 5;             /* velocidade da repeticao */
                  }
               }
            }
            if (a && !a_prev)
               zx_libretro_osk_select();
            if (b && !b_prev)
               zx_libretro_osk_close();
         }
         r_prev = r;
         a_prev = a;
         b_prev = b;
         dir_prev = (uint16_t)(mask & ((1u << RETRO_DEVICE_ID_JOYPAD_UP)   |
                                      (1u << RETRO_DEVICE_ID_JOYPAD_DOWN) |
                                      (1u << RETRO_DEVICE_ID_JOYPAD_LEFT) |
                                      (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT)));

         if (was_on || zx_libretro_osk_active())
            osk_block |= (uint16_t)(mask & osk_bits);
         osk_block &= mask;                       /* solto = desbloqueado */
         mask &= (uint16_t)~osk_block;
      }
      /* Os 4 botoes de fogo do Kempston de 8 bits do Next (jogos de 3 e 4
       * botoes, como o Deltas Shadow):
       *   B = botao 1, A = botao 2, Y = botao 3, X = botao 4.
       * O glue liga os quatro. Com o teclado virtual aberto, o jogo nao recebe
       * nenhum botao. */
      if (port == 0 && zx_libretro_osk_active())
         mask &= (uint16_t)~((1u << RETRO_DEVICE_ID_JOYPAD_Y) |
                             (1u << RETRO_DEVICE_ID_JOYPAD_X) |
                             (1u << RETRO_DEVICE_ID_JOYPAD_A) |
                             (1u << RETRO_DEVICE_ID_JOYPAD_B));
      zx_set_joy(port, mask);
   }

#ifdef ZX_TRACE
   QueryPerformanceCounter(&q1);
#endif
   zx_libretro_osk_tick();   /* solta a tecla enviada pelo teclado virtual */
   zx_run_frame();
#ifdef ZX_TRACE
   QueryPerformanceCounter(&q2);
#endif

   fb = zx_get_framebuffer(&w, &h, &pitch);
   if (w != cur_width || h != cur_height)
   {
      struct retro_game_geometry geom;
      zx_av_info info;

      zx_get_av_info(&info);
      memset(&geom, 0, sizeof(geom));
      geom.base_width   = w;
      geom.base_height  = h;
      geom.max_width    = info.max_width;
      geom.max_height   = info.max_height;
      geom.aspect_ratio = info.aspect;
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
      cur_width  = w;
      cur_height = h;
   }

#ifdef ZX_TRACE
   QueryPerformanceCounter(&q3);
#endif
   if (fb)
      video_cb(fb, w, h, pitch);
   else
      video_cb(NULL, w, h, pitch); /* repete o frame anterior */
#ifdef ZX_TRACE
   QueryPerformanceCounter(&q4);
#endif

   /* ---------------------------------------------------------------- audio ---
 *
 * Quantas amostras de audio pertencem a UM quadro de video:
 *
 *     amostras_por_quadro = taxa_de_audio / quadros_por_segundo
 *
 * Com 15600 Hz e 50 fps, isso da 312. E 312 e' exatamente o que o ZEsarUX
 * produz: medido, envio_audio() roda a cada 5 quadros (FRAMES_VECES_BUFFER_AUDIO
 * em audio.h:37) e empurra 1560 amostras -- 1560/5 = 312 por quadro.
 *
 * O que estava errado: pediamos AUDIO_MAX_FRAMES (2048). O core entregava
 * 2048 amostras por quadro a 15600 Hz, ou seja 131 ms de audio a cada 20 ms
 * de video -- 6,56 vezes audio demais, dos quais 88% eram silencio
 * manufactured pelo proprio driver. Medido antes da correcao:
 *
 *     1010 blocos, 2.068.480 amostras, 236.032 com sinal (11,4%)
 *     808 de 1010 blocos totalmente mudos
 *
 * Alem disso o driver tinha um bug de conta (a diferenca write-read num
 * size_t sem sinal), que so aparecia depois do primeiro wrap do buffer.
 *
 * Por que 312 e' o numero certo e nao um conveniente: se o core entregasse
 * mais audio do que o quadro de video consome, o frontend tocaria esse
 * excedente na taxa anunciada e o som sairia desacelerado, com estalos
 * entre os blocos. Entregar menos faz o oposto. O certo e' os dois lados
 * concordarem, e os dois medem 312.
 *
 * AUDIO_MAX_FRAMES continua sendo o TAMANHO DO BUFFER; so nao e' mais o
 * tanto que se pede por quadro.
 */
if (audio_frames_por_quadro > AUDIO_MAX_FRAMES)
      audio_frames_por_quadro = AUDIO_MAX_FRAMES;

frames = zx_get_audio(audio_buf, audio_frames_por_quadro);
   if (frames)
      audio_batch_cb(audio_buf, frames);

#ifdef ZX_TRACE
   {
      LARGE_INTEGER q5;
      double d0 = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)fq.QuadPart;
      double d1 = (double)(q2.QuadPart - q1.QuadPart) * 1000.0 / (double)fq.QuadPart;
      double d2 = (double)(q3.QuadPart - q2.QuadPart) * 1000.0 / (double)fq.QuadPart;
      double d3 = (double)(q4.QuadPart - q3.QuadPart) * 1000.0 / (double)fq.QuadPart;
      QueryPerformanceCounter(&q5);
      double d4 = (double)(q5.QuadPart - q4.QuadPart) * 1000.0 / (double)fq.QuadPart;
      static unsigned n = 0;
      fprintf(stderr, "[run] %u entrada %.2f emulacao %.2f get_fb %.2f video_cb %.2f audio %.2f TOTAL %.2f\n",
              ++n, d0, d1, d2, d3, d4,
              (double)(q5.QuadPart - q0.QuadPart) * 1000.0 / (double)fq.QuadPart);
   }
#endif
}

/* ------------------------------------------------------------------------ */
/* Save states                                                              */
/* ------------------------------------------------------------------------ */
size_t retro_serialize_size(void)
{
   return core_initialized ? zx_state_size() : 0;
}

bool retro_serialize(void *data, size_t size)
{
   return core_initialized && zx_state_save(data, size);
}

bool retro_unserialize(const void *data, size_t size)
{
   return core_initialized && zx_state_load(data, size);
}

/* ------------------------------------------------------------------------ */
/* Cheats e memoria (nao implementados)                                     */
/* ------------------------------------------------------------------------ */
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index; (void)enabled; (void)code;
}

void  *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
