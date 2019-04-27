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


  This module deals with audio input from pulseaudio, and has been taken & adapted from
  the CAVA (Console-based Audio Visualizer for ALSA) project:
    https://github.com/karlstav/cava/ (copy taken from 6f46ca1d033f89d332c8c0a8f6abc699aa71bf6d / Sat Feb 16 18:46:30 2019 +0100)

*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pthread.h>

#include "audio.h"
#include "input.h"

#define BUFSIZE 4096

int _debug_pulse = 0;

struct input_pulse_ctx
{
  pa_mainloop *m_pulseaudio_mainloop;
  struct audio_data *audio;
  int terminate;  // shared variable used to terminate audio thread
  int thr_id;
  pthread_t p_thread;
};

static void cb (pa_context *pulseaudio_context, const pa_server_info *i, void *userdata)
{
  //getting default sink name
  struct input_pulse_ctx *ctx = (struct input_pulse_ctx *)userdata;
  struct audio_data *audio = ctx->audio;

  /* If a source wasn't specified in the config file, use the default one */
  if (audio->source == NULL)
  {
    audio->source = malloc(sizeof(char) * 1024);

    strcpy(audio->source,i->default_sink_name);

    //appending .monitor suufix
    audio->source = strcat(audio->source, ".monitor");
  }

  //quiting mainloop
  pa_context_disconnect(pulseaudio_context);
  pa_context_unref(pulseaudio_context);
  pa_mainloop_quit(ctx->m_pulseaudio_mainloop, 0);
  pa_mainloop_free(ctx->m_pulseaudio_mainloop);
}

static void pulseaudio_context_state_callback(pa_context *pulseaudio_context, void *userdata)
{
  struct input_pulse_ctx *ctx = userdata;

  //make sure loop is ready
  switch (pa_context_get_state(pulseaudio_context))
  {
    case PA_CONTEXT_UNCONNECTED:
      if (_debug_pulse) printf("UNCONNECTED\n");
      break;
    case PA_CONTEXT_CONNECTING:
      if (_debug_pulse) printf("CONNECTING\n");
      break;
    case PA_CONTEXT_AUTHORIZING:
      if (_debug_pulse) printf("AUTHORIZING\n");
      break;
    case PA_CONTEXT_SETTING_NAME:
      if (_debug_pulse) printf("SETTING_NAME\n");
      break;
    case PA_CONTEXT_READY://extract default sink name
      if (_debug_pulse) printf("READY\n");
      pa_operation_unref(pa_context_get_server_info(
      pulseaudio_context, cb, userdata));
      break;
    case PA_CONTEXT_FAILED:
      if (_debug_pulse) printf("failed to coennect to pulseaudio server\n");
      exit(EXIT_FAILURE);
      break;
    case PA_CONTEXT_TERMINATED:
      if (_debug_pulse) printf("TERMINATED\n");
      pa_mainloop_quit(ctx->m_pulseaudio_mainloop, 0);
      break;
  }
}

/* init */
static int getPulseDefaultSink(void **ctx, struct audio_data *audio, config_t *cfg)
{
  *ctx = calloc(1, sizeof(struct input_pulse_ctx));
  struct input_pulse_ctx *c = *ctx;
  config_setting_t *cfg_root;
  const char *str = NULL;

  c->audio = audio;
  pa_mainloop_api *mainloop_api;
  pa_context *pulseaudio_context;
  int ret;
  c->audio = audio;

  /* Get source from config (if present) */
  cfg_root = config_root_setting(cfg);
  config_setting_lookup_string(cfg_root, "pulse_source", &str);
  if (str != NULL && strlen(str) > 0)
  {
    if (c->audio->source)
      free(c->audio->source);
    c->audio->source = calloc(1, strlen(str)+1);
    strcpy(c->audio->source, str);
  }

  // Create a mainloop API and connection to the default server
  c->m_pulseaudio_mainloop = pa_mainloop_new();

  mainloop_api = pa_mainloop_get_api(c->m_pulseaudio_mainloop);
  pulseaudio_context = pa_context_new(mainloop_api, "cava device list");


  // This function connects to the pulse server
  pa_context_connect(pulseaudio_context, NULL, PA_CONTEXT_NOFLAGS, NULL);


  if (_debug_pulse) printf("connecting to pulseaudio server\n");

  //This function defines a callback so the server will tell us its state.
  pa_context_set_state_callback(pulseaudio_context,
                                pulseaudio_context_state_callback,
                                (void*)c);


  //starting a mainloop to get default sink

  //starting with one nonblokng iteration in case pulseaudio is not able to run
  if (!(ret = pa_mainloop_iterate(c->m_pulseaudio_mainloop, 0, &ret)))
  {
    printf("Could not open pulseaudio mainloop to "
           "find default device name: %d\n"
           "check if pulseaudio is running\n",
           ret);

     return -1;
  }

  pa_mainloop_run(c->m_pulseaudio_mainloop, &ret);
  printf("Using audio source: %s\n", audio->source);
  return 0;
}


/* run */
static void* input_pulse(void *ctx)
{
  int i, n;
  int16_t buf[BUFSIZE / 2];

  struct input_pulse_ctx *c = ctx;
  struct audio_data *audio = c->audio;

  /* The sample type to use */
  static const pa_sample_spec ss =
  {
    .format = PA_SAMPLE_S16LE,
    .rate =  44100,
    .channels = 2
  };

  static const pa_buffer_attr pb =
  {
    .maxlength = (uint32_t) -1, //BUFSIZE * 2,
    .fragsize = BUFSIZE
  };

  pa_simple *s = NULL;
  int error;

  if (!(s = pa_simple_new(NULL, "cava", PA_STREAM_RECORD, audio->source, "audio for cava", &ss, NULL, &pb, &error))) 
  {
    //fprintf(stderr, __FILE__": Could not open pulseaudio source: %s, %s. To find a list of your pulseaudio sources run 'pacmd list-sources'\n",audio->source, pa_strerror(error));
    printf(__FILE__": Could not open pulseaudio source: %s, %s. To find a list of your pulseaudio sources run 'pacmd list-sources'\n",audio->source, pa_strerror(error));
    c->terminate = 1;
    printf("input_pulse EXIT-1\n");
    pthread_exit(NULL);
  }

  n = 0;

  while (1)
  {
    /* Record some data ... */
    if (pa_simple_read(s, buf, sizeof(buf), &error) < 0) 
    {
      //fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
      //exit(EXIT_FAILURE);
      printf(__FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
      c->terminate = 1;
      printf("input_pulse EXIT-2\n");
      pthread_exit(NULL);
    }

    //sorting out channels

    for (i = 0; i < BUFSIZE / 2; i += 2)
    {
      if (audio->channels == 1)
        audio->audio_out_l[n] = (buf[i] + buf[i + 1]) / 2;

      //stereo storing channels in buffer
      if (audio->channels == 2)
      {
        audio->audio_out_l[n] = buf[i];
        audio->audio_out_r[n] = buf[i + 1];
      }

      n++;
      if (n == 2048 - 1)
        n = 0;
    }

    if (c->terminate == 1)
    {
      pa_simple_free(s);
      if (_debug_pulse) printf("input_pulse EXIT-3\n");
      break;
    }
  }

  return NULL;
}

static void input_start(void *ctx)
{
  struct input_pulse_ctx *c = ctx;

  c->thr_id = pthread_create(&c->p_thread, NULL, input_pulse, ctx);
}

static void cleanup(void **ctx)
{
  struct input_pulse_ctx *c = *ctx;

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
  struct input_pulse_ctx *c = *ctx;

  c->terminate = 1;
  pthread_join(c->p_thread, NULL);
}

int pulse_register(struct audio_input *ai)
{
  strcpy(ai->name, "pulse");
  ai->init    = getPulseDefaultSink;
  ai->run     = input_start;
  ai->stop    = stop;
  ai->cleanup = cleanup;
  return 0;
}
