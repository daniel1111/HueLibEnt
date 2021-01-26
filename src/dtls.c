/*
 * DTLS - heavily based on:
 *   https://bitbucket.org/tiebingzhang/tls-psk-server-client-example/src/783092f802383421cfa1088b0e7b804b39d3cf7c (Tiebing Zhang)
 */

#include "dtls.h"

#define MAX_PAYLOAD_SIZE 1350

static int dtls_ctx_index;

static void debug(struct dtls_ctx *ctx, int level, char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  if (ctx->debug_level >= level)
  {
    if (ctx->debug_callback)
    {
      char buffer[1024];
      vsnprintf(buffer, sizeof(buffer)-1, fmt, args);
      buffer[sizeof(buffer)-1] = '\0';
      ctx->debug_callback(buffer, ctx->user_data);
    }
    else
    {
      printf("[dtls] ");
      vprintf(fmt, args);
      puts("");
    }
  }
  va_end(args);
}

static int cval(char c)
{
  if (c>='a') return c-'a'+0x0a;
  if (c>='A') return c-'A'+0x0a;
  return c-'0';
}

static int hex2bin(char *str, unsigned char *out)
{
  int i;
  for(i = 0; str[i] && str[i+1]; i+=2)
  {
    if (!isxdigit(str[i])&& !isxdigit(str[i+1]))
      return -1;
    out[i/2] = (cval(str[i])<<4) + cval(str[i+1]);
  }
  return i/2;
}

static unsigned int psk_cb(SSL *ssl, const char *hint, char *identity,
  unsigned int max_identity_len, unsigned char *psk, unsigned int max_psk_len)
{
  int ret;
  struct dtls_ctx *ctx;

  ctx = SSL_get_ex_data(ssl, dtls_ctx_index);

  if (!hint)
    debug(ctx, MSG_DEBUG, "NULL received PSK identity hint, continuing anyway");
  else
    debug(ctx, MSG_DEBUG, "Received PSK identity hint '%s'", hint);

  ret = snprintf(identity, max_identity_len, "%s", ctx->psk_identity);
  if (ret < 0 || (unsigned int)ret > max_identity_len)
  {
    debug(ctx, MSG_ERR, "Error, psk_identify too long");
    return 0;
  }

  if (strlen(ctx->psk_key)>=(max_psk_len*2))
  {
    debug(ctx, MSG_ERR,  "Error, psk_key too long");
    return 0;
  }

  /* convert the PSK key to binary */
  ret = hex2bin(ctx->psk_key,psk);
  if (ret<=0)
  {
    debug(ctx, MSG_ERR, "Error, Could not convert PSK key '%s' to binary key", ctx->psk_key);
    return 0;
  }
  return ret;
}

int dtls_send_data(struct dtls_ctx *ctx, void *buf, int length)
{
  int len;
  int retval=0;

  if (ctx->state != DTLS_STATE_CONNECTED)
  {
    debug(ctx, MSG_ERR, "dtls_send_data> dtls_ctx wrong state (%d vs expected %d)", ctx->state, DTLS_STATE_CONNECTED);
    return -1;
  }

  debug(ctx, MSG_DEBUG, "about to write %d bytes", length);
  len = SSL_write(ctx->ssl, buf, length);

  switch (SSL_get_error(ctx->ssl, len))
  {
    case SSL_ERROR_NONE:
      debug(ctx, MSG_DEBUG, "wrote %d bytes", (int) len);
      break;

    case SSL_ERROR_WANT_WRITE:
      /* Just try again later */
      break;

    case SSL_ERROR_WANT_READ:
      /* continue with reading */
      break;

    case SSL_ERROR_SYSCALL:
      debug(ctx, MSG_ERR, "Socket write error: ");
      debug(ctx, MSG_ERR, "%s (%d)", ERR_error_string(ERR_get_error(), buf), SSL_get_error(ctx->ssl, len));
      retval = -1;
      break;

    case SSL_ERROR_SSL:
      debug(ctx, MSG_ERR, "SSL write error: ");
      debug(ctx, MSG_ERR, "%s (%d)", ERR_error_string(ERR_get_error(), buf), SSL_get_error(ctx->ssl, len));
      retval = -1;
      break;

    default:
      debug(ctx, MSG_ERR, "Unexpected error while writing!");
      retval = -1;
      break;
  }

  return retval;
}

int dtls_init(struct dtls_ctx *ctx, const char *psk_identity, const char *psk_key, debug_cb_t debug_callback, int debug_level)
{
  static int first_run = 1;
  memset(ctx, 0, sizeof(struct dtls_ctx));
  ctx->debug_callback = debug_callback;
  ctx->debug_level = debug_level;

  debug(ctx, MSG_INFO, "dtls_init() %s", first_run==1 ? "(first run)" : "");

  if (first_run)
  {
    first_run = 0;
    OpenSSL_add_ssl_algorithms();
    SSL_load_error_strings();
    dtls_ctx_index = SSL_get_ex_new_index(0, "dtls_ctx index", NULL, NULL, NULL);
  }

  ctx->psk_identity = malloc(strlen(psk_identity)+1);
  ctx->psk_key = malloc(strlen(psk_key)+1);
  strcpy(ctx->psk_identity, psk_identity);
  strcpy(ctx->psk_key, psk_key);

  ctx->fd = -1;
  ctx->state = DTLS_STATE_INIT;
  return 0;
}

void dtls_cleanup(struct dtls_ctx *ctx)
{
  debug(ctx, MSG_INFO, "dtls_cleanup() %s");

  if (ctx->ssl_ctx)
  {
    SSL_CTX_free(ctx->ssl_ctx);
    ctx->ssl_ctx = NULL;
  }

  if (ctx->ssl)
  {
    SSL_shutdown(ctx->ssl);
    SSL_free(ctx->ssl);
    ctx->ssl = NULL;
  }

  if (ctx->psk_identity)
  {
    free(ctx->psk_identity);
    ctx->psk_identity = NULL;
  }

  if (ctx->psk_key)
  {
    free(ctx->psk_key);
    ctx->psk_key = NULL;
  }

  if (ctx->fd != -1)
  {
    close(ctx->fd);
    ctx->fd = -1;
  }

  ctx->state = DTLS_STATE_CLEANEDUP;
}

int dtls_connect(struct dtls_ctx *ctx, const char *address, int port)
{
  int retval;
  char err_buf[200];

  if (ctx->state != DTLS_STATE_INIT)
  {
    debug(ctx, MSG_ERR, "dtls_connect> dtls_ctx wrong state (%d vs expected %d)", ctx->state, DTLS_STATE_INIT);
    return -1;
  }

  debug(ctx, MSG_INFO, "Connecting to %s:%d", address, port);

  memset((void *) &ctx->remote_addr, 0, sizeof(struct sockaddr_in));
  memset((void *) &ctx->local_addr , 0, sizeof(struct sockaddr_in));

  if (inet_pton(AF_INET, address, &ctx->remote_addr.sin_addr) == 1)
  {
    ctx->remote_addr.sin_family = AF_INET;
    ctx->remote_addr.sin_port = htons(port);
  }
  else
  {
    debug(ctx, MSG_ERR, "inet_pton failed");
  }

  ctx->fd = socket(ctx->remote_addr.sin_family, SOCK_DGRAM, 0);
  if (ctx->fd < 0)
  {
    debug(ctx, MSG_ERR, "Failed to create socket");
    return -1;
  }

  ctx->ssl_ctx = SSL_CTX_new(DTLS_client_method());
  SSL_CTX_set_min_proto_version(ctx->ssl_ctx, DTLS1_2_VERSION);
  SSL_CTX_set_psk_client_callback(ctx->ssl_ctx, psk_cb);

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
  SSL_CTX_set_ciphersuites(ctx->ssl_ctx, "TLS_PSK_WITH_AES_128_GCM_SHA256");
#endif

#ifdef __APPLE__
  SSL_CTX_set_options(ctx->ssl_ctx, SSL_OP_NO_QUERY_MTU);
#endif

  ctx->ssl = SSL_new(ctx->ssl_ctx);

  /* Save a reference to the dtls ctx struct so callback can access it */
  SSL_set_ex_data(ctx->ssl, dtls_ctx_index, ctx);

#ifdef __APPLE__
  SSL_set_mtu(ctx->ssl, MAX_PAYLOAD_SIZE);
#endif

  /* Create BIO, connect and set to already connected */
  ctx->bio = BIO_new_dgram(ctx->fd, BIO_CLOSE);
  connect(ctx->fd, (struct sockaddr *) &ctx->remote_addr, sizeof(struct sockaddr_in));
  BIO_ctrl(ctx->bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &ctx->remote_addr);
#ifdef __APPLE__
  BIO_ctrl(ctx->bio, BIO_CTRL_DGRAM_SET_MTU, MAX_PAYLOAD_SIZE, NULL);
#endif
  SSL_set_bio(ctx->ssl, ctx->bio, ctx->bio);

  retval = SSL_connect(ctx->ssl);
  if (retval <= 0)
  {
    switch (SSL_get_error(ctx->ssl, retval)) 
    {
      case SSL_ERROR_ZERO_RETURN:
        debug(ctx, MSG_ERR, "SSL_connect failed with SSL_ERROR_ZERO_RETURN");
        break;
      case SSL_ERROR_WANT_READ:
        debug(ctx, MSG_ERR, "SSL_connect failed with SSL_ERROR_WANT_READ");
        break;
      case SSL_ERROR_WANT_WRITE:
        debug(ctx, MSG_ERR, "SSL_connect failed with SSL_ERROR_WANT_WRITE");
        break;
      case SSL_ERROR_WANT_CONNECT:
        debug(ctx, MSG_ERR, "SSL_connect failed with SSL_ERROR_WANT_CONNECT");
        break;
      case SSL_ERROR_WANT_ACCEPT:
        debug(ctx, MSG_ERR, "SSL_connect failed with SSL_ERROR_WANT_ACCEPT");
        break;
      case SSL_ERROR_WANT_X509_LOOKUP:
        debug(ctx, MSG_ERR, "SSL_connect failed with SSL_ERROR_WANT_X509_LOOKUP");
        break;
      case SSL_ERROR_SYSCALL:
        debug(ctx, MSG_ERR, "SSL_connect failed with SSL_ERROR_SYSCALL");
        break;
      case SSL_ERROR_SSL:
        debug(ctx, MSG_ERR, "SSL_connect failed with SSL_ERROR_SSL");
        break;
      default:
        debug(ctx, MSG_ERR, "SSL_connect failed with unknown error");
        break;
    }
    debug(ctx, MSG_ERR, "%s (%d)", ERR_error_string(ERR_get_error(), err_buf), SSL_get_error(ctx->ssl, sizeof(err_buf)));
    debug(ctx, MSG_ERR, "errno = %d (%s)", errno, strerror(errno));
    return -1;
  }

  debug(ctx, MSG_DEBUG, "Connected; Cipher: %s", SSL_CIPHER_get_name(SSL_get_current_cipher(ctx->ssl)));
  ctx->state = DTLS_STATE_CONNECTED;
  return 0;
}
