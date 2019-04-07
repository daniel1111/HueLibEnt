/*
Copyright (c) 2015 Karl Stavestrand <karl@stavestrand.no>
Copyright (c) 2019 Daniel Swann <github@dswann.co.uk>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


  This module deals with audio processing, and has been taken & adapted from
  the CAVA (Console-based Audio Visualizer for ALSA) project:
    https://github.com/karlstav/cava/ (copy taken from 6f46ca1d033f89d332c8c0a8f6abc699aa71bf6d / Sat Feb 16 18:46:30 2019 +0100)

*/

#include <stdlib.h>

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <termios.h>
#include <math.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <fftw3.h>
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <getopt.h>
#include <pthread.h>
#include <dirent.h>
#include <ctype.h>

#define M 2048

#include "audio.h"

enum cava_mode {CAVAMODE_1_LIGHT_FREQ = 1, CAVAMODE_2_COLOUR_FREQ = 2};
   
static double _smoothDef[64] = {0.8, 0.8, 1, 1, 0.8, 0.8, 1, 0.8, 0.8, 1, 1, 0.8,
          1, 1, 0.8, 0.6, 0.6, 0.7, 0.8, 0.8, 0.8, 0.8, 0.8,
          0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8,
          0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8,
          0.7, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6};

 static int *separate_freq_bands(fftw_complex out[M / 2 + 1], int bars, int lcf[200],
       int hcf[200], float k[200], int channel, double sens, double ignore)
{
  int o,i;
  float peak[201];
  static int fl[200];
  static int fr[200];
  int y[M / 2 + 1];
  float temp;

  // process: separate frequency bands
  for (o = 0; o < bars; o++)
  {

    peak[o] = 0;

    // process: get peaks
    for (i = lcf[o]; i <= hcf[o]; i++)
    {
      //getting r of compex
      y[i] = hypot(out[i][0], out[i][1]);
      peak[o] += y[i]; //adding upp band
    }

    peak[o] = peak[o] / (hcf[o]-lcf[o]+1); //getting average
    temp = peak[o] * k[o] * sens; //multiplying with k and adjusting to sens settings
    if (temp <= ignore)
      temp = 0;
    if (channel == 1)
      fl[o] = temp;
    else 
      fr[o] = temp;
  }

  if (channel == 1)
    return fl;
   else
     return fr;
}

struct cava_ctx
{
  pthread_t  p_thread;
  int thr_id;
  float fc[200];
  float fre[200];
  int f[200], lcf[200], hcf[200];
  int *fl, *fr;
  int fmem[200];
  int flast[200];
  int flastd[200];
  int sleep /*= 0 */;
  int i, n, o, height, w, c, rest, inAtty, silence, fp, fptest;
  //int cont = 1;
  int fall[200];
  //float temp;
  float fpeak[200];
  float k[200];
  float g;
  struct timespec req; // = { .tv_sec = 0, .tv_nsec = 0 };
  char configPath[255];


  char ch; //= '\0';
  //double inr[2 * (M / 2 + 1)];
  double inl[2 * (M / 2 + 1)];
  int bars; // = 25;
  //char supportedInput[255];// = "'fifo'";
  int sourceIsAuto; // = 1;
  double smh;
  double sens;           // sensitivity
  fftw_complex outl[M / 2 + 1];
//  fftw_complex outr[M / 2 + 1];
  fftw_plan pl;
 // fftw_plan pr;
  bool senseLow;
  //int maxvalue = 0;

  int mode;
  int light_count;
  config_t *cfg;

  struct audio_data *audio;
};

static void hue_output(struct cava_ctx *ctx, struct hue_ent_ctx *ctx_ent)
{
  if (ctx->mode == CAVAMODE_1_LIGHT_FREQ)
  {
    for (int i = 0; i < ctx->light_count; i++)
    {
      uint16_t val = (ctx->f[i] < 65534 ? ctx->f[i] : 65534);
      hue_ent_set_light(ctx_ent, i, val, val, val);
    }
  }
  else if (ctx->mode == CAVAMODE_2_COLOUR_FREQ)
  {
    for (int i = 0; i < ctx->light_count; i++)
    {
      int r, g, b;
      int comp = 65534;

      r = (ctx->f[0] < comp ? ctx->f[0] : comp);
      g = (ctx->f[1] < comp ? ctx->f[1] : comp);
      b = (ctx->f[2] < comp ? ctx->f[2] : comp);

      hue_ent_set_light(ctx_ent, i, r, g, b);
    }
  }
  return;
}

static int proces_cava_init(void **c, struct audio_data *audio, int light_count, config_t *cfg)
{
  config_setting_t *cfg_root;

  *c = calloc(1, sizeof(struct cava_ctx));
  struct cava_ctx *ctx = *c;
//fft: planning to rock
  ctx->pl = fftw_plan_dft_r2c_1d(M, ctx->inl, ctx->outl, FFTW_MEASURE);
  ctx->sens = 1.0;
  ctx->senseLow = true;
  ctx->audio = audio;
  ctx->light_count = light_count;
  ctx->cfg = cfg;

  /* Get mode from config */
  cfg_root = config_root_setting(cfg);
  config_setting_lookup_int(cfg_root, "cava_mode", &ctx->mode);
  if (ctx->mode < CAVAMODE_1_LIGHT_FREQ || ctx->mode > CAVAMODE_2_COLOUR_FREQ)
  {
    printf("Invalid cava_mode! (%d)\n", ctx->mode);
    return -1;
  }

  return 0;
}

static void proces_cava_cleanup(void **ctx)
{
  struct cava_ctx *c = *ctx;

  fftw_destroy_plan(c->pl);

  if (*ctx)
  {
    free(*ctx);
    *ctx = NULL;
  }
}

static int print_raw_out(int bars_count,
int ascii_range, char *bar_delim, char *frame_delim, const int const f[200]) {
 // ascii
		for (int i = 0; i < bars_count; i++)
    {
			int f_ranged = f[i];
			if (f_ranged > ascii_range)
        f_ranged = ascii_range;

			// finding size of number-string in byte
			int bar_height_size = 2; // a number + \0
			if (f_ranged != 0) 
        bar_height_size += floor (log10 (f_ranged));

			char bar_height[bar_height_size];
			snprintf(bar_height, bar_height_size, "%d", f_ranged);

			//printf(bar_height);
      printf("%d", f[i]);
			printf(bar_delim);
		}
	printf(frame_delim);

	return 0;
}


static void process_audio(void *c, struct hue_ent_ctx *ctx_ent)
{
  struct cava_ctx *ctx = c;
  double gravity = 100;
  int framerate = 60;
  int smcount = 64;
  unsigned int lowcf  = 50;    // lower_cutoff_freq
  unsigned int highcf = 10000; // higher_cutoff_freq

  double integral = 90.0 / 100.0;
  int autosens = 1;

  // ctx->w = 200; //width must be hardcoded for raw output.
  ctx->height = 65534; /*p.ascii_range; */

  if (ctx->mode == CAVAMODE_1_LIGHT_FREQ)
    ctx->bars = ctx->light_count;
  else /* CAVAMODE_2_COLOUR_FREQ */
    ctx->bars = 3;

  ctx->g = gravity * ((float)ctx->height / 2160) * pow((60 / (float)framerate), 2.5);


  if ((smcount > 0) && (ctx->bars > 0))
    ctx->smh = (double)(((double)smcount)/((double)ctx->bars));

  double freqconst = log10((float)lowcf / (float)highcf) /
    ((float)1 / ((float)ctx->bars + (float)1) - 1);


  // process: calculate cutoff frequencies
  for (int n = 0; n < ctx->bars + 1; n++)
  {
    ctx->fc[n] = highcf * pow(10, freqconst * (-1) + ((((float)n + 1) /
      ((float)ctx->bars + 1)) * freqconst));
    ctx->fre[n] = ctx->fc[n] / (ctx->audio->rate / 2);
    // remember nyquist!, pr my calculations this should be rate/2
    // and  nyquist freq in M/2 but testing shows it is not...
    // or maybe the nq freq is in M/4

    // lfc stores the lower cut frequency foo each bar in the fft out buffer
    ctx->lcf[n] = ctx->fre[n] * (M /2);
    if (n != 0)
    {
      ctx->hcf[n - 1] = ctx->lcf[n] - 1;

      // pushing the spectrum up if the expe function gets "clumped"
      if (ctx->lcf[n] <= ctx->lcf[n - 1])
        ctx->lcf[n] = ctx->lcf[n - 1] + 1;
      ctx->hcf[n - 1] = ctx->lcf[n] - 1;
    }
  }
  // process: weigh signal to frequencies
  for (int n = 0; n < ctx->bars; n++)
  {
    ctx->k[n] = pow(ctx->fc[n],0.85) * ((float)ctx->height/(M*32000)) *
      _smoothDef[(int)floor(((double)n) * ctx->smh)];
  }

  // process: populate input buffer and check if input is present
  ctx->silence = 1;
  for (int i = 0; i < (2 * (M / 2 + 1)); i++)
  {
    if (i < M)
    {
      ctx->inl[i] = ctx->audio->audio_out_l[i];
      if (ctx->inl[i])
        ctx->silence = 0;
    }
    else
    {
      ctx->inl[i] = 0;
    }
  }

  if (ctx->silence == 1)
    ctx->sleep++;
  else
    ctx->sleep = 0;

  // process: if input was present for the last 5 seconds apply FFT to it
  if (ctx->sleep < framerate * 5)
  {
    // process: execute FFT and sort frequency bands
    fftw_execute(ctx->pl);
    ctx->fl = separate_freq_bands(ctx->outl,ctx->bars,ctx->lcf,ctx->hcf, ctx->k, 1, ctx->sens, 0);
  }
  else
  {
    // printf("(no audio)\n");
    return;
  }

  //preperaing signal for drawing
  for (int o = 0; o < ctx->bars; o++)
    ctx->f[o] = ctx->fl[o];

  // process [smoothing]: falloff
  if (ctx->g > 0)
  {
    for (int o = 0; o < ctx->bars; o++)
    {
      if (ctx->f[o] < ctx->flast[o])
      {
        ctx->f[o] = ctx->fpeak[o] - (ctx->g * ctx->fall[o] * ctx->fall[o]);
        ctx->fall[o]++;
      }
      else
      {
        ctx->fpeak[o] = ctx->f[o];
        ctx->fall[o] = 0;
      }

      ctx->flast[o] = ctx->f[o];
    }
  }

  // process [smoothing]: integral

  if (integral > 0)
  {
    for (int o = 0; o < ctx->bars; o++)
    {
      ctx->f[o] = ctx->fmem[o] * integral + ctx->f[o];
      ctx->fmem[o] = ctx->f[o];

      int diff = (ctx->height + 1) - ctx->f[o];
      if (diff < 0)
        diff = 0;
      double div = 1 / (diff + 1);

      ctx->fmem[o] = ctx->fmem[o] * (1 - div / 20);
    }
  }

  // zero values causes divided by zero segfault
  for (int o = 0; o < ctx->bars; o++)
  {
    if (ctx->f[o] < 1)
      ctx->f[o] = 0;
  }

  // automatic sens adjustment
  if (autosens)
  {
    for (int o = 0; o < ctx->bars; o++)
    {
      if (ctx->f[o] > ctx->height)
      {
        ctx->senseLow = false;
        ctx->sens = ctx->sens * 0.985;
        break;
      }
      if (ctx->senseLow && !ctx->silence)
        ctx->sens = ctx->sens * 1.01;

      if (o == ctx->bars - 1)
        ctx->sens = ctx->sens * 1.002;
    }
  }

  //printf("sens=%f, autosens=%d\n",ctx->sens, autosens);

  //print_raw_out(ctx->bars, ctx->height , ":", "\n",ctx->f);

  hue_output(ctx, ctx_ent);
}

int cava_register(struct audio_process *ap)
{
  strcpy(ap->name, "cava");
  ap->init    = proces_cava_init;
  ap->process = process_audio;
  ap->cleanup = proces_cava_cleanup;
  return 0;
}

