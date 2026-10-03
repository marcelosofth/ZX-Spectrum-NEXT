/*
 * selftest.c - carrega o core e exercita a API fora do frontend.
 *
 * Serve para achar onde o core quebra sem a GUI do RetroArch no meio. Ele
 * simula o frontend: implementa os callbacks que o core pede e vai chamando
 * a API na ordem em que o RetroArch chamaria.
 *
 * Cada passo imprime o nome ANTES de executar, entao o ultimo passo impresso
 * antes de o processo morrer e o culpado.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <windows.h>

#include "libretro.h"

static unsigned cb_frames;
static unsigned total_frames;

/* ------------------------------------------------ callbacks do frontend --- */

static void cb_log(enum retro_log_level level, const char *fmt, ...)
{
    va_list va;
    printf("        [core] ");
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
}

static void cb_video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    (void)data;
    if (cb_frames < 3)
        printf("\n        video %ux%u pitch %llu\n", w, h, (unsigned long long)pitch);
    cb_frames++;
}

static void cb_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t cb_audio_batch(const int16_t *d, size_t f);

/* ------------------------------------------------------------------------
 * DIAGNOSTICO DE AUDIO
 *
 * O driver pode entregar 2048 amostras por quadro de video quando so 312
 * tem som -- 111 ms de silencio a cada 20 ms de audio. Para medir em vez
 * de supor, conto quantas amostras chegam com sinal de verdade. */
static unsigned long long aud_total_amostras = 0;
static unsigned long long aud_com_sinal       = 0;
static unsigned long long aud_blocos          = 0;
static unsigned long long aud_blocos_vazios   = 0;
static unsigned long long aud_por_bloco       = 0;
static int               aud_pico            = 0;

static size_t cb_audio_diag(const int16_t *d, size_t f)
{
    size_t i;
    int com = 0;
    aud_blocos++;
    aud_por_bloco += (unsigned long long)f;
    for (i = 0; i < f; i++)
    {
        aud_total_amostras++;
        if (d[i * 2] != 0 || d[i * 2 + 1] != 0)
        {
            aud_com_sinal++;
            com = 1;
            int p = d[i * 2] < 0 ? -d[i * 2] : d[i * 2];
            if (p > aud_pico) aud_pico = p;
        }
    }
    if (!com) aud_blocos_vazios++;
    return f;
}
static void cb_input_poll(void) { }

static int16_t cb_input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{
    (void)p; (void)d; (void)i; (void)id;
    return 0;
}

static bool cb_environment(unsigned cmd, void *data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        {
            struct retro_log_callback *lc = (struct retro_log_callback *)data;
            lc->log = cb_log;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
            printf("\n        av_info %ux%u fps %.2f audio %.0f",
                   ((struct retro_system_av_info *)data)->geometry.base_width,
                   ((struct retro_system_av_info *)data)->geometry.base_height,
                   ((struct retro_system_av_info *)data)->timing.fps,
                   ((struct retro_system_av_info *)data)->timing.sample_rate);
            return true;

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            printf("\n        pixel_format %d", (int)*(const enum retro_pixel_format *)data);
            return true;

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        {
            /* O core procura system_dir/zesarux/ para os dois arquivos do
             * Next. Passar a propria pasta da fonte nao serve mais: la o
             * caminho seria <fonte>/zesarux, e existe ali um ARQUIVO chamado
             * zesarux (o executavel), nao uma pasta.
             *
             * Por isso o system_dir e' a pasta que CONTEM a subpasta
             * zesarux/. Para testar o layout de distribuicao de verdade. */
            const char **d = (const char **)data;
            *d = getenv("ZX_TEST_SYSTEM_DIR")
               ? getenv("ZX_TEST_SYSTEM_DIR")
               : "G:/Internet_Temp/Batocera_add/Projetos/NEXT/system_test";
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            struct retro_variable *v = (struct retro_variable *)data;
            if (!strcmp(v->key, "zesarux_refresh")) v->value = "50 Hz";
            else v->value = "3.5MHz";
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *(bool *)data = false;
            return true;

        default:
            return true;   /* ignora o resto */
    }
}

/* ------------------------------------------------------------------ main --- */

#define PASSO(texto) printf("  %-30s", texto), fflush(stdout)
#define FIM       printf("ok\n")

static void *sym(HMODULE h, const char *nome)
{
    void *p = (void *)GetProcAddress(h, nome);
    if (!p)
    {
        printf("  FALTA O SIMBOLO %s\n", nome);
        exit(1);
    }
    return p;
}

int main(int argc, char **argv)
{
    const char *dll  = (argc > 1) ? argv[1] : "zesarux_libretro.dll";
    const char *jogo = (argc > 2) ? argv[2] : NULL;

    HMODULE h = LoadLibraryA(dll);
    if (!h) { printf("nao carregou %s (erro %lu)\n", dll, GetLastError()); return 1; }
    printf("carregou %s\n", dll);
    printf("jogo: %s\n\n", jogo ? jogo : "(nenhum)");
    fflush(stdout);

    /* Ponteiros de funcao, preenchidos com GetProcAddress. */
    unsigned   (*api_version)(void)                = NULL;
    void      (*set_environment)(retro_environment_t) = NULL;
    void      (*set_video_refresh)(retro_video_refresh_t) = NULL;
    void      (*set_audio_sample)(retro_audio_sample_t) = NULL;
    void      (*set_audio_sample_batch)(retro_audio_sample_batch_t) = NULL;
    void      (*set_input_poll)(retro_input_poll_t)  = NULL;
    void      (*set_input_state)(retro_input_state_t) = NULL;
    void      (*init)(void)                         = NULL;
    void      (*deinit)(void)                       = NULL;
    void      (*get_system_info)(struct retro_system_info *) = NULL;
    void      (*get_system_av_info)(struct retro_system_av_info *) = NULL;
    bool      (*load_game)(const struct retro_game_info *) = NULL;
    void      (*unload_game)(void)                  = NULL;
    void      (*run)(void)                          = NULL;
    void      (*reset)(void)                        = NULL;
    size_t    (*serialize_size)(void)               = NULL;

    api_version             = (unsigned (*)(void))sym(h, "retro_api_version");
    set_environment         = (void (*)(retro_environment_t))sym(h, "retro_set_environment");
    set_video_refresh       = (void (*)(retro_video_refresh_t))sym(h, "retro_set_video_refresh");
    set_audio_sample        = (void (*)(retro_audio_sample_t))sym(h, "retro_set_audio_sample");
    set_audio_sample_batch  = (void (*)(retro_audio_sample_batch_t))sym(h, "retro_set_audio_sample_batch");
    set_input_poll          = (void (*)(retro_input_poll_t))sym(h, "retro_set_input_poll");
    set_input_state         = (void (*)(retro_input_state_t))sym(h, "retro_set_input_state");
    init                    = (void (*)(void))sym(h, "retro_init");
    deinit                  = (void (*)(void))sym(h, "retro_deinit");
    get_system_info         = (void (*)(struct retro_system_info *))sym(h, "retro_get_system_info");
    get_system_av_info      = (void (*)(struct retro_system_av_info *))sym(h, "retro_get_system_av_info");
    load_game               = (bool (*)(const struct retro_game_info *))sym(h, "retro_load_game");
    unload_game             = (void (*)(void))sym(h, "retro_unload_game");
    run                     = (void (*)(void))sym(h, "retro_run");
    reset                   = (void (*)(void))sym(h, "retro_reset");
    serialize_size          = (size_t (*)(void))sym(h, "retro_serialize_size");

    printf("=== a ordem que o RetroArch usa ===\n");

    PASSO("retro_api_version");
    printf("%u", api_version());
    FIM;

    set_environment(cb_environment);
    set_video_refresh(cb_video);
    set_audio_sample(cb_audio_sample);
    set_audio_sample_batch(cb_audio_diag);
    set_input_poll(cb_input_poll);
    set_input_state(cb_input_state);
    printf("  callbacks registrados");
    FIM;

    {
        struct retro_system_info si;
        PASSO("retro_get_system_info");
        get_system_info(&si);
        printf("\n        %s %s ext=%s", si.library_name, si.library_version, si.valid_extensions);
        FIM;
    }

    PASSO("retro_init  <-- suspecto n.1");
    init();
    FIM;

    {
        struct retro_system_av_info av;
        PASSO("retro_get_system_av_info");
        get_system_av_info(&av);
        printf("\n        %ux%u fps %.2f audio %.0f",
               av.geometry.base_width, av.geometry.base_height,
               av.timing.fps, av.timing.sample_rate);
        FIM;
    }

    {
        struct retro_game_info gi;
        struct retro_game_info *p = NULL;
        if (jogo) { memset(&gi, 0, sizeof gi); gi.path = jogo; p = &gi; }
        PASSO("retro_load_game  <-- suspecto n.2");
        if (!load_game(p)) { printf("devolveu false\n"); return 2; }
        FIM;
    }

    PASSO("retro_run x1010  <-- suspecto n.3");
    /* Medir a CPU da EMULACAO separada da CPU da CARGA.
     *
     * Sem isso, "tempo de CPU por quadro" mistura as duas coisas e nao
     * significa nada: carregar o SD de 1 GB para RAM custa segundos de CPU
     * (256 mil page faults) e de I/O, e isso entra na media como se fosse
     * emulacao. E' o que fazia a conta de 8,2 ms por quadro nao fechar com
     * os 12,5 ms que o relogio do quadro mede. */
    {
        FILETIME ft0, ft1, ik0, ik1;
        ULARGE_INTEGER u0, u1;
        LARGE_INTEGER q0, q1, fq;
        GetProcessTimes(GetCurrentProcess(), &ft0, &ft0, &ik0, &ik0);
        QueryPerformanceFrequency(&fq);
        QueryPerformanceCounter(&q0);
        for (total_frames = 0; total_frames < 1010; total_frames++)
        {
            /* NAO imprimir nada por quadro.
             *
             * O harness antigo fazia printf + fflush a CADA quadro, com a
             * saida redirecionada para arquivo. Isso e' um write() de verdade
             * por quadro: a thread bloqueava em I/O, e o tempo de CPU do
             * processo ficava ~47% do tempo de parede. Ou seja, metade do
             * "quadro lento" era o harness medindo a si mesmo.
             *
             * Se quiser o progresso de novo, imprima a cada 100 quadros. */
            if ((total_frames % 100) == 0)
            {
                printf("\r  %u/1010", total_frames);
                fflush(stdout);
            }
            run();
        }
        QueryPerformanceCounter(&q1);
        GetProcessTimes(GetCurrentProcess(), &ft1, &ft1, &ik1, &ik1);
        u0.LowPart = ik0.dwLowDateTime; u0.HighPart = ik0.dwHighDateTime;
        u1.LowPart = ik1.dwLowDateTime; u1.HighPart = ik1.dwHighDateTime;
        {
            /* GetProcessTimes(kernel, user) em unidades de 100 ns.
             * c0/c1 nao servem: sao tempo de criacao e de saida do processo. */
            double cpu_s = (double)(u1.QuadPart - u0.QuadPart) / 10000000.0;
            double wall_s = (double)(q1.QuadPart - q0.QuadPart) / (double)fq.QuadPart;
            fprintf(stdout, "\r  EMULACAO: parede %.2f s   CPU %.2f s   -> %.2f ms por quadro\n",
                    wall_s, cpu_s, wall_s * 1000.0 / 1010.0);
            fprintf(stdout, "  CPU por quadro %.2f ms  _thread fora da CPU_ %.0f%% do tempo\n",
                    cpu_s * 1000.0 / 1010.0,
                    100.0 * (1.0 - (cpu_s / wall_s)));
        }
    }
    printf("\r  60/60                             ");
    printf("ok (%u frames de video)\n", cb_frames);

    printf("  audio: %llu blocos, %llu amostras, %llu COM SINAL (%.1f%%), %llu blocos vazios\n",
           aud_blocos, aud_total_amostras, aud_com_sinal,
           aud_total_amostras ? 100.0 * (double)aud_com_sinal / (double)aud_total_amostras : 0.0,
           aud_blocos_vazios);
    printf("  audio: amostras por bloco = %llu   pico = %d\n",
           aud_blocos ? aud_por_bloco / aud_blocos : 0, aud_pico);
    fflush(stdout);

    PASSO("retro_serialize_size");
    printf("%llu", (unsigned long long)serialize_size());
    FIM;

    PASSO("retro_unload_game");
    unload_game();
    FIM;

    PASSO("retro_deinit");
    deinit();
    FIM;

    printf("\nTUDO PASSOU\n");
    return 0;
}