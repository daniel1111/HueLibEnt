#pragma once

#include <stdio.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/err.h>
#include <openssl/dh.h>
#include <openssl/ssl.h>
#include <openssl/conf.h>
#include <openssl/engine.h>
#include <openssl/opensslv.h>
#include <math.h>
#include <ctype.h>
#include "hue_debug.h"

#define HUE_DTLS_MAX_PAYLOAD_SIZE 1350
#define HUE_DTLS_STATE_INIT      10
#define HUE_DTLS_STATE_CONNECTED 20
#define HUE_DTLS_STATE_CLEANEDUP 30

struct hue_dtls_ctx
{
  SSL_CTX *ssl_ctx;
  SSL  *ssl;
  BIO  *bio;
  struct sockaddr_in remote_addr;
  struct sockaddr_in local_addr;
  int  port;
  int  fd;
  char *psk_identity;
  char *psk_key;
  int  state;
  void *user_data;
  int  debug_level;
  hue_debug_cb_t debug_callback;
};

/* Function: hue_dtls_init

   Initialise a new hue_dtls_ctx object. Be sure to call dtls_cleanup when finished with ctx.

   Parameters:

      ctx - hue_dtls_ctx object to initialise
      psk_identity - Pre-shared key identity for the session
      psk_key - Pre-shared key for the session
      debug_callback - (optional) debug callback to receive debug messages. Set to NULL to print to STDOUT.
      debug_level - about of debugging output to generate. One of: MSG_OFF, MSG_ERR, MSG_INFO or MSG_DEBUG.

   Returns:

      0 on success, non-zero otherwise
*/
int  hue_dtls_init(struct hue_dtls_ctx *ctx, const char *psk_identity, const char *psk_key, hue_debug_cb_t debug_callback, int debug_level);

/* Function: hue_dtls_connect

   Start dtls connection

   Parameters:

      ctx - Initialised hue_dtls_ctx object
      address - IP address to connect to
      port - port number

   Returns:

      0 on success, non-zero otherwise
*/
int  hue_dtls_connect(struct hue_dtls_ctx *ctx, const char *address, int port);

/* Function: hue_dtls_send_data

   Send data on connecion

   Parameters:

      ctx - Connected hue_dtls_ctx object
      buf - data to send
      port - length of data to send

   Returns:

      0 on success, non-zero otherwise
*/
int  hue_dtls_send_data(struct hue_dtls_ctx *ctx, void *buf, int length);

/* Function: hue_dtls_cleanup

   Disconnect if connected, and free any memory associated with dtls context.

   Parameters:

      ctx - hue_dtls_ctx object
*/
void hue_dtls_cleanup(struct hue_dtls_ctx *ctx);
