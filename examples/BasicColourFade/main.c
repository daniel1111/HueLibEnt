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

#include "dtls.h"
#include "hue_entertainment.h"
#include "hue_rest.h"

#define DTLS_PORT 2100
#define SSL_PORT  443

void print_usage(const char* name)
{
  printf("\nHue lights colour cycle test\n");
  printf("Usage: %s -a <ip address> -i <identity> -p <psk> [-d <debug level>]\n\n", name);

  printf("Parameters:\n");
  printf("    -a <ip address>  IP address of Hue Bridge\n");
  printf("    -i <identity>    Identity to use when connecting to bridge\n");
  printf("    -p <psk>         Pre Shared Key to use when connecting to bridge\n");
  printf("    -d <level>       Debug level 0-3. Default: 1 (errors only)\n");
  printf("\n");
}

int main (int argc, char **argv)
{
  const char *identity = NULL;
  const char *psk = NULL;
  const char *ip_address = NULL;
  int c;
  int debug_level = MSG_ERR;
  struct dtls_ctx ctx_dtls;
  struct hue_ent_ctx ctx_ent;
  struct hue_rest_ctx ctx_hr;
  struct hue_entertainment_area *ent_areas;
  void *msg_buf;
  int buf_len;
  int ent_areas_count;
  int framerate = 80;
  int interval_ms;

  while ((c = getopt (argc, argv, "i:p:a:d:")) != -1)
  {
    switch (c)
      {
      case 'i': /* Identity */
        identity = optarg;
        break;

      case 'p': /* Pre-shared key */
        psk = optarg;
        break;

      case 'a': /* Address */
        ip_address = optarg;
        break;

      case 'd': /* Debug level */
        debug_level = atoi(optarg);
        break;

      case 'h':
      case 'H':
        print_usage(argv[0]);
        exit(0);
        break;

      case '?':
        print_usage(argv[0]);
        exit(-1);
        break;

      default:
        return -1;
      }
  }

  if (!identity)
  {
    printf("\nERROR: Identity not provided\n");
    print_usage(argv[0]);
    return -1;
  }

  if (!psk)
  {
    printf("\nERROR: psk not provided\n");
    print_usage(argv[0]);
    return -1;
  }

  if (!ip_address)
  {
    printf("\nERROR: IP address not provided\n");
    print_usage(argv[0]);
    return -1;
  }

  hue_rest_init();
  hue_rest_init_ctx(&ctx_hr, NULL, ip_address, SSL_PORT, identity, debug_level);

  printf("Getting entertainment areas\n");
  hue_rest_get_ent_groups(&ctx_hr, &ent_areas, &ent_areas_count);

  if (ent_areas_count <= 0)
  {
    printf("No entertainment areas found.\n");
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    return -1;
  }

  /* Count how many lights are in the entertainment area */
  int light_count;
  for (light_count=0; light_count < MAX_LIGHTS_PER_AREA; light_count++)
    if (ent_areas->light_ids[light_count] == 0)
      break;

  if (light_count == 0)
  {
    printf("No lights found in entertainment area [%s].\n", ent_areas->area_name);
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    return -2;
  }

  /* Activate the entertainment area */
  printf("Enabling entertainment area [%s]\n", ent_areas->area_name);
  hue_rest_activate_stream(&ctx_hr, ent_areas->area_id);

  /* Initialise hue entertainment context */
  hue_ent_init(&ctx_ent, light_count);

  /* Assign the light ID (as returned by the bridge) to each light to be controlled (0..n) */
  for (int n = 0; n < light_count; n++)
    hue_ent_set_light_id(&ctx_ent, n, ent_areas->light_ids[n]);

  /* Connect to bridge using DTLS */
  printf("Making DTLS connection to bridge\n");
  dtls_init(&ctx_dtls, identity, psk, NULL, debug_level);
  int retval = dtls_connect(&ctx_dtls, ip_address, DTLS_PORT);
  if (retval)
  {
    printf("Failed to make DTLS connection to bridge (retval=%d)\n", retval);
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    dtls_cleanup(&ctx_dtls);
    hue_ent_cleanup(&ctx_ent);
    return -3;
  }

  interval_ms = 1000 / framerate;

  printf("Running...\n");

  uint16_t r=255;
  uint16_t g=0;
  uint16_t b=0;
  int light=0;
  printf("Light = %d (id = %d)\n", light, ent_areas->light_ids[light]);

  /* Do colour cycling */
  while (1)
  {
    usleep(interval_ms*1000);

    if (r > 0 && b == 0)
    {
      r--;
      g++;
    }

    if (g > 0 && r == 0)
    {
      g--;
      b++;
    }

    if (b > 0 && g == 0)
    {
      r++;
      b--;
    }

    if (b == 0 && g == 0)
    {
      hue_ent_set_light(&ctx_ent, light, 0, 0, 0);
      if (++light >= light_count)
        light = 0;
      printf("Light = %d (id = %d)\n", light, ent_areas->light_ids[light]);
    }

    /* hue_ent_set_light expects the values for r/g/b to be 0..65535, but the generated values
     * are 0..255. So shift values left by 8 bits to get 0..65535 */
    hue_ent_set_light(&ctx_ent, light, r << 8, g << 8, b << 8);

    /* Generate message */
    hue_ent_get_message(&ctx_ent, &msg_buf, &buf_len);

    /* Send message */
    if (dtls_send_data(&ctx_dtls, msg_buf, buf_len))
    {
      printf("Connection lost, exiting...\n");
      break;
    }
  }

  hue_rest_cleanup_ctx(&ctx_hr);
  hue_rest_cleanup();
  dtls_cleanup(&ctx_dtls);
  hue_ent_cleanup(&ctx_ent);

  return 0;
}
