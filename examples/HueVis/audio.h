#pragma once
#include <libconfig.h>
#include "hue_entertainment.h"

/* "struct audio_data" stolen from input/fifo.c in cava:
 * https://github.com/karlstav/cava
 */

struct audio_data
{
  int audio_out_r[2048];
  int audio_out_l[2048];
  int format;
  unsigned int rate;
  char *source;   // alsa device, fifo path or pulse source
  int im;         // input mode alsa, fifo or pulse
  int channels;
  char error_message[1024];
};

typedef int (*audio_init_cb_t)(void **ctx, struct audio_data *data, config_t *cfg);
typedef int (*audiop_init_cb_t)(void **ctx, struct audio_data *data, int light_count, config_t *cfg);
typedef void (*audio_run_cb_t)(void *ctx);
typedef void (*audio_process_cb_t)(void *ctx, struct hue_ent_ctx *ctx_ent);
typedef void (*audio_stop_cb_t)(void **ctx);
typedef void (*audio_cleanup_cb_t)(void **ctx);

struct audio_input
{
  char               name[50];
  audio_init_cb_t    init;
  audio_run_cb_t     run;  /* Function started in a seperate thread */
  audio_stop_cb_t    stop;
  audio_cleanup_cb_t cleanup;
  struct audio_input *next;
};

struct audio_process
{
  char               name[50];
  audiop_init_cb_t   init;
  audio_process_cb_t process;
  audio_cleanup_cb_t cleanup;
};
