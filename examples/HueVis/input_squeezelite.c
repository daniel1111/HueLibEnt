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


  This module deals with audio input from squeezelite, and has been taken & adapted from
  the CAVA (Console-based Audio Visualizer for ALSA) project:
    https://github.com/karlstav/cava/ (copy taken from 6f46ca1d033f89d332c8c0a8f6abc699aa71bf6d / Sat Feb 16 18:46:30 2019 +0100)

*/

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "audio.h"
#include "input.h"

typedef unsigned int u32_t;
typedef short s16_t;

#define BUFSIZE 2048


#define VIS_BUF_SIZE 16384
#define VB_OFFSET 8192+4096

struct input_squeezelite_ctx
{
  struct audio_data *audio;
  int terminate;  // shared variable used to terminate audio thread
  int thr_id;
  pthread_t p_thread;
};

typedef struct
{
  pthread_rwlock_t rwlock;
  u32_t buf_size;
  u32_t buf_index;
  bool running;
  u32_t rate;
  time_t updated;
  s16_t buffer[VIS_BUF_SIZE];
} vis_t;

/* init */
static int squeezelite_init(void **ctx, struct audio_data *audio, config_t *cfg)
{
  *ctx = calloc(1, sizeof(struct input_squeezelite_ctx));
  struct input_squeezelite_ctx *c = *ctx;
  const char *str;
  config_setting_t *cfg_root;

  c->audio = audio;

  /* Get source from config */
  cfg_root = config_root_setting(cfg);
  config_setting_lookup_string(cfg_root, "squeezelite_source", &str);
  if (str == NULL || strlen(str) == 0)
  {
    printf("Failed to get squeezelite_source from config\n");
    free(*ctx);
    return -1;
  }

  c->audio->source = calloc(1, strlen(str)+1);
  strcpy(c->audio->source, str);

  return 0;
}

// input: SHMEM / squeezelite
void* input_shmem(void* ctx)
{
  struct input_squeezelite_ctx *c = ctx;
  struct audio_data *audio = c->audio;

  vis_t *mmap_area;
  int fd; /* file descriptor to mmaped area */
  int mmap_count = sizeof( vis_t);
  int n = 0;
  int i;

  printf("input_shmem: source: %s\n", audio->source);

  fd = shm_open(audio->source, O_RDWR, 0666);

  if (fd < 0 )
  {
    printf("Could not open source '%s': %s\n", audio->source, strerror( errno ) );
    exit(EXIT_FAILURE);
  }
  else
  {
    mmap_area = mmap(NULL, sizeof( vis_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((intptr_t)mmap_area == -1)
    {
      printf("mmap failed - check if squeezelite is running with visualization enabled\n");
      exit(EXIT_FAILURE);
    }
  }
  // printf("bufs: %u / run: %u / rate: %u\n",mmap_area->buf_size, mmap_area->running, mmap_area->rate);
  audio->rate = mmap_area->rate;

  while (1)
  {
    for (i = VB_OFFSET; i < BUFSIZE+VB_OFFSET; i += 2)
    {
      if (audio->channels == 1)
      {
        audio->audio_out_l[n] = (mmap_area->buffer[i] + mmap_area->buffer[i + 1]) / 2;
      }
      else if (audio->channels == 2)
      {
        audio->audio_out_l[n] = mmap_area->buffer[i];
        audio->audio_out_r[n] = mmap_area->buffer[i + 1];
      }
      n++;
      if (n == 2048 - 1) n = 0;
    }
    if (c->terminate == 1)
    {
      break;
    }
  }

  // cleanup
  if ( fd > 0 )
  {
    if ( close( fd ) != 0 )
    {
      printf("Could not close file descriptor %d: %s", fd, strerror( errno ) );
    }
  }
  else
  {
    printf("Wrong file descriptor %d", fd );
  }

  if (munmap( mmap_area, mmap_count ) != 0)
  {
    printf("Could not munmap() area %p+%d. %s", mmap_area, mmap_count, strerror( errno ) );
  }

  return 0;
}

static void input_start(void *ctx)
{
  struct input_squeezelite_ctx *c = ctx;

  c->thr_id = pthread_create(&c->p_thread, NULL, input_shmem, ctx);
}

static void cleanup(void **ctx)
{
  struct input_squeezelite_ctx *c = *ctx;

  if (c)
  {
    if (c->audio->source)
    {
      free(c->audio->source);
      c->audio->source = NULL;
    }
    free(c);
    c = NULL;
  }
}

static void stop(void **ctx)
{
  struct input_squeezelite_ctx *c = *ctx;

  c->terminate = 1;
  pthread_join(c->p_thread, NULL);
}

int squeezelite_register(struct audio_input *ai)
{
  strcpy(ai->name, "squeezelite");
  ai->init    = squeezelite_init;
  ai->run     = input_start;
  ai->stop    = stop;
  ai->cleanup = cleanup;
  return 0;
}
