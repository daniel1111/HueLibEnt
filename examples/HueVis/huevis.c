/*
 * Copyright (c) 2019, Daniel Swann <github@dswann.co.uk>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <unistd.h>
#include <libconfig.h>
#include <signal.h>

#include "dtls.h"
#include "hue_entertainment.h"
#include "hue_rest.h"

#include "audio.h"
#include "input.h"
#include "process.h"

#define VERSION "0.1"

#define DTLS_PORT 2100
#define SSL_PORT  443

#define LEN_USERNAME 100
#define LEN_PSK      100
#define LEN_IP       20

static volatile int ctrlc = 0;
static struct audio_input *_audio_inputs = NULL;

void int_handler(int signum)
{
  (void)(signum);
  printf("Exiting\n");
  ctrlc = 1;
}

void register_input(int (*input_reg)(struct audio_input*))
{
  struct audio_input *ai;

  if (!_audio_inputs)
  {
    _audio_inputs = calloc(1, sizeof(struct audio_input));
    input_reg(_audio_inputs);
    return;
  }

  ai = _audio_inputs;

  /* Move to end of list */
  while (ai->next)
    ai = ai->next;

  ai->next = calloc(1, sizeof(struct audio_input));
  input_reg(ai->next);
}

void register_inputs()
{
  register_input(pulse_register);
  register_input(squeezelite_register);
}

void list_inputs()
{
  struct audio_input *ai = _audio_inputs;

  while (ai)
  {
    printf("\t%s\n", ai->name);
    ai = ai->next;
  }
  printf("\n");
}


void print_usage(const char* name)
{
  printf("\nHue lights audio visualiser %s\n", VERSION);
  printf("Usage: %s [options]\n", name);
  printf("Visualise audio input using Hue lights\n\n");

  printf("Options:\n");
  printf("    -r <ip address>           Register with bridge at <ip address>. On success, (over)writes bridge_credentials.conf\n");
  printf("    -c <config file>          Use <config file>\n");
  printf("    -p <credentials file>     Use <credentials file>\n");
  printf("    -a                        List possible audio inputs and exit\n");
  printf("\n");
  printf("Use CTRL-C to exit\n\n");
}

/* Get credentials to connect to the bridge. If cmdline_ipaddress is null, get config from file. If it's set, try to
 * register with the bridge.
 * Returns:
 *  0 on SUCCESS -  connection_username/connection_psk/connection_ip populated
 * -1 on FAILURE
 */
int get_bridge_credentials(const char *name, const char *credentials_filename, const char *cmdline_ipaddress, char *connection_username, char *connection_psk, char *connection_ip)
{
  config_t cfg_credentials;
  config_setting_t *cfg_root, *cfg_setting;
  const char *username, *psk, *ip;

  /* See if we already have saved credentials for the bridge */
  if ((access(credentials_filename, F_OK ) == -1 ) && (cmdline_ipaddress == NULL))
  {
     /* credentials file doesn't exist, and -r flag not given */
     printf("Must register with the bridge first using \"%s -r <ip address>\" (%s not found)\n",
            name, credentials_filename);
     return -1;
  }

  config_set_options(&cfg_credentials, (CONFIG_OPTION_AUTOCONVERT));
  config_init(&cfg_credentials);

  /* If the -r flag was passed, try registering with the bridge, and save the result */
  if (cmdline_ipaddress != NULL)
  {
    /* The -r flag has been passed, so we should be trying to register with the bridge */
    char *out_username;
    char *out_clientkey;
    int retval;
    struct hue_rest_ctx ctx_hr;

    hue_rest_init();
    hue_rest_init_ctx(&ctx_hr, NULL, cmdline_ipaddress, SSL_PORT, "", MSG_ERR);
    retval = hue_rest_register(&ctx_hr, &out_username, &out_clientkey);
    if (retval)
    {
      /* hue_rest_register should already have output some kind of error to stdout */
      retval = -1;
    }
    else
    {
      /* save settings to config file */
     // config_init(&cfg_credentials);
      cfg_root = config_root_setting(&cfg_credentials);
      cfg_setting = config_setting_add(cfg_root, "connection_username", CONFIG_TYPE_STRING);
      config_setting_set_string(cfg_setting, out_username);

      cfg_setting = config_setting_add(cfg_root, "connection_psk", CONFIG_TYPE_STRING);
      config_setting_set_string(cfg_setting, out_clientkey);

      cfg_setting = config_setting_add(cfg_root, "connection_ip", CONFIG_TYPE_STRING);
      config_setting_set_string(cfg_setting, cmdline_ipaddress);

      if (!config_write_file(&cfg_credentials, credentials_filename))
      {
        printf("Registered with hue bridge, but failed to save config!\n");
        printf("username = %s\n", out_username);
        printf("psk      = %s\n", out_clientkey);
        retval = -1;
      }
      else
      {
        printf("Registered with hue bridge, config saved\n");
        retval = 0;
      }

      strncpy(connection_username, out_username     , LEN_USERNAME); connection_username[LEN_USERNAME-1] = '\0';
      strncpy(connection_psk     , out_clientkey    , LEN_PSK     ); connection_psk[LEN_USERNAME-1]      = '\0';
      strncpy(connection_ip      , cmdline_ipaddress, LEN_IP      ); connection_ip[LEN_USERNAME-1]       = '\0';
    }

    config_destroy(&cfg_credentials);
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    return retval;
  }

  /* -r flag not given, and a credentials file was found. try opening it */
  if (!config_read_file(&cfg_credentials, credentials_filename))
  {
    fprintf(stderr, "Error reading [%s]: %s:%d - %s\n", credentials_filename, config_error_file(&cfg_credentials),
            config_error_line(&cfg_credentials), config_error_text(&cfg_credentials));
    config_destroy(&cfg_credentials);
    return -1;
  }

  cfg_root = config_root_setting(&cfg_credentials);
  config_setting_lookup_string(cfg_root, "connection_username", &username);
  if (username == NULL || strlen(username)==0)
  {
    printf("connection_username not found in %s\n", credentials_filename);
    config_destroy(&cfg_credentials);
    return -1;
  }

  config_setting_lookup_string(cfg_root, "connection_psk", &psk);
  if (psk == NULL || (strlen(psk)==0))
  {
    printf("connection_psk not found in %s\n", credentials_filename);
    config_destroy(&cfg_credentials);
    return -1;
  }

  config_setting_lookup_string(cfg_root, "connection_ip", &ip);
  if (ip == NULL || (strlen(ip)==0))
  {
    printf("connection_ip not found in %s\n", credentials_filename);
    config_destroy(&cfg_credentials);
    return -1;
  }

  strncpy(connection_username, username, LEN_USERNAME); connection_username[LEN_USERNAME-1] = '\0';
  strncpy(connection_psk     , psk     , LEN_PSK     ); connection_psk[LEN_USERNAME-1]      = '\0';
  strncpy(connection_ip      , ip      , LEN_IP      ); connection_ip[LEN_USERNAME-1]       = '\0';

  config_destroy(&cfg_credentials);
  return 0;
}

int get_audio_input(config_t *cfg, struct audio_input **ai)
{
  config_setting_t *cfg_root;
  const char *ai_in_config;
  struct audio_input *ai_ptr = _audio_inputs;

  cfg_root = config_root_setting(cfg);
  config_setting_lookup_string(cfg_root, "audio_input", &ai_in_config);
  if (ai_in_config == NULL || strlen(ai_in_config) == 0)
  {
    printf("Failed to get audio_input from config\n");
    return -1;
  }

  /* Find the audio input */
  while (ai_ptr)
  {
    if (!strcmp(ai_in_config, ai_ptr->name))
    {
      *ai = ai_ptr;
      return 0;
    }
    ai_ptr = ai_ptr->next;
  }

  printf("Unknown audio_input: %s\n", ai_in_config);
  printf("Valid options are:\n");
  list_inputs();

  return -1;
}

int main (int argc, char **argv)
{
  struct audio_input *ai;
  struct audio_data audio;
  struct audio_process ap;
  void *audio_ctx;
  void *process_ctx;

  struct dtls_ctx ctx_dtls;
  struct hue_ent_ctx ctx_ent;
  struct hue_rest_ctx ctx_hr;
  struct hue_entertainment_area *ent_areas;
  int ent_areas_count;
  int framerate = 60;
  int interval_ms;
  config_t cfg_config;
  char connection_username[LEN_USERNAME] = "";
  char connection_psk[LEN_PSK] = "";
  char connection_ip[LEN_IP] = "";
  const char *default_config_filename = "huevis.conf";
  const char *default_credentials_filename = "bridge_credentials.conf";

  void *msg_buf;
  int buf_len;
  char *cmdline_ipaddress = NULL;
  char *cmdline_config_file = NULL;
  char *cmdline_credentials_file = NULL;
  int c;

  register_inputs();

  /*
   * -r register <IP address>
   */
  while ((c = getopt (argc, argv, "ahHr:c:p:")) != -1)
  {
    switch (c)
      {
      case 'r':
        cmdline_ipaddress = optarg;
        break;

      case 'c':
        cmdline_config_file = optarg;
        break;

      case 'p':
        cmdline_credentials_file = optarg;
        break;

      case 'a':
        printf("Registered audio inputs:\n");
        list_inputs();
        exit(0);
        break;

      case 'h':
      case 'H':
        print_usage(argv[0]);
        exit(0);
        break;

      default:
        return -1;
      }
  }

  if (get_bridge_credentials(argv[0], (cmdline_credentials_file ? cmdline_credentials_file : default_credentials_filename), cmdline_ipaddress, connection_username, connection_psk, connection_ip))
  {
    printf("Failed to get credentials to connect to bridge\n");
    return -1;
  }

  /* Now open the main config file */
  config_set_options(&cfg_config, (CONFIG_OPTION_AUTOCONVERT));
  if (!config_read_file(&cfg_config, (cmdline_config_file ? cmdline_config_file : default_config_filename)))
  {
    fprintf(stderr, "Error reading [%s]: %s:%d - %s\n", (cmdline_config_file ? cmdline_config_file : default_config_filename), config_error_file(&cfg_config),
            config_error_line(&cfg_config), config_error_text(&cfg_config));
    config_destroy(&cfg_config);
    return -1;
  }

  hue_rest_init();
  hue_rest_init_ctx(&ctx_hr, NULL, connection_ip, SSL_PORT, connection_username, MSG_ERR);

  printf("Getting entertainment areas\n");
  hue_rest_get_ent_groups(&ctx_hr, &ent_areas, &ent_areas_count);

  if (ent_areas_count <= 0)
  {
    printf("No entertainment areas found.\n");
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    config_destroy(&cfg_config);
    return -1;
  }

  /* So we've found 1 or more entertainment areas. For now, always just
   * use the first (pointed to by ent_areas).
   */

  /* Count how many lights are in the entertainment area */
  int light_count;
  for (light_count=0; light_count < MAX_LIGHTS_PER_AREA; light_count++)
    if (ent_areas->light_ids[light_count] == 0)
      break;

  if (light_count == 0)
  {
    printf("No lights found in entertainment area [%s].\n", ent_areas->area_name);
    config_destroy(&cfg_config);
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    return -2;
  }

  /* Activate the entertainment area */
  printf("Enabling entertainment area [%s]\n", ent_areas->area_name);
  hue_rest_activate_stream(&ctx_hr, ent_areas->area_id);

  /* Big assumption: light IDs are in an order that makes sense (e.g. left to right). 
   * This probably isn't the case. TODO: the brige does provide x/y position information
   * for the bulbs, so should use that to figure out a better order */
  hue_ent_init(&ctx_ent, light_count);
  for (int n = 0; n < light_count; n++)
    hue_ent_set_light_id(&ctx_ent, n, ent_areas->light_ids[n]);

  /* Connect to bridge using DTLS */
  printf("Making DTLS connection to bridge\n");
  dtls_init(&ctx_dtls, connection_username, connection_psk, NULL, MSG_ERR);
  int retval = dtls_connect(&ctx_dtls, connection_ip, DTLS_PORT);
  if (retval)
  {
    printf("Failed to make DTLS connection to bridge (retval=%d)\n", retval);
    config_destroy(&cfg_config);
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    dtls_cleanup(&ctx_dtls);
    hue_ent_cleanup(&ctx_ent);
    return -3;
  }

  interval_ms = 1000 / framerate;
  memset (&audio, 0, sizeof(audio));
  audio.channels = 1;
  audio.rate = 44100;

  if (get_audio_input(&cfg_config, &ai))
  {
    config_destroy(&cfg_config);
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    dtls_cleanup(&ctx_dtls);
    hue_ent_cleanup(&ctx_ent);
    return -5;
  }
  cava_register(&ap);

  printf("Init audio\n");
  if (ai->init(&audio_ctx, &audio, &cfg_config))
  {
    config_destroy(&cfg_config);
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    dtls_cleanup(&ctx_dtls);
    hue_ent_cleanup(&ctx_ent);
    return -1;
  }

  printf("Start audio capture & processing\n");
  ai->run(audio_ctx);
  ap.init(&process_ctx, &audio, light_count, &cfg_config);

  signal(SIGINT, int_handler);
  printf("Running...\n");

  while (!ctrlc)
  {
    usleep(interval_ms*1000);
    ap.process(process_ctx, &ctx_ent);

    hue_ent_get_message(&ctx_ent, &msg_buf, &buf_len);

    if (dtls_send_data(&ctx_dtls, msg_buf, buf_len))
    {
      printf("Connection lost, exiting...\n");
      config_destroy(&cfg_config);
      ai->stop(&audio_ctx);
      ai->cleanup(&audio_ctx);
      ap.cleanup(&process_ctx);
      hue_rest_cleanup_ctx(&ctx_hr);
      hue_rest_cleanup();
      dtls_cleanup(&ctx_dtls);
      hue_ent_cleanup(&ctx_ent);
      return -4;
    }
  }

  ai->stop(&audio_ctx);
  ai->cleanup(&audio_ctx);
  ap.cleanup(&process_ctx);
  config_destroy(&cfg_config);
  hue_rest_cleanup_ctx(&ctx_hr);
  hue_rest_cleanup();
  dtls_cleanup(&ctx_dtls);
  hue_ent_cleanup(&ctx_ent);
  return 0;
}
