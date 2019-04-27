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

#include "hue_entertainment.h"

int hue_ent_init(struct hue_ent_ctx *ctx, int light_count)
{
  memset(ctx, 0, sizeof(struct hue_ent_ctx));
  ctx->light_count = light_count;

  /* Allocate memory for light data */
  ctx->data = calloc(light_count, sizeof(struct hue_ent_message_data));
  if (!ctx->data)
    return -1;

  memcpy(ctx->header.protocol_name, "HueStream", sizeof("HueStream")-1);
  ctx->header.version_major = 1;
  ctx->header.version_minor = 0;

  /* Allocate memory for full message */
  ctx->buf_size = sizeof(struct hue_ent_message_header) + (light_count * sizeof(struct hue_ent_message_data));
  ctx->msg_buf = malloc(ctx->buf_size);
  if (!ctx->msg_buf)
    return -1;

  memset(ctx->msg_buf, 0, ctx->buf_size);

  return 0;
}

int hue_ent_set_light_id(struct hue_ent_ctx *ctx, int index, uint16_t hue_light_id)
{
  struct hue_ent_message_data *data;

  if (index >= ctx->light_count)
    return -1;

  data = &ctx->data[index];
  data->id = htons(hue_light_id);
  return 0;
}

int hue_ent_set_light(struct hue_ent_ctx *ctx, int index, uint16_t R, uint16_t G, uint16_t B)
{
  struct hue_ent_message_data *data;

  if (index >= ctx->light_count)
    return -1;

  data = &ctx->data[index];
  data->R = htons(R);
  data->G = htons(G);
  data->B = htons(B);

  return 0;
}

int hue_ent_get_message(struct hue_ent_ctx *ctx, void **out_msg_buf, int *out_buf_len)
{
  memcpy(ctx->msg_buf, &ctx->header, sizeof(struct hue_ent_message_header));
  memcpy((uint8_t*)ctx->msg_buf+sizeof(struct hue_ent_message_header), ctx->data,  (ctx->light_count * sizeof(struct hue_ent_message_data))); 
  
  *out_msg_buf = ctx->msg_buf;
  *out_buf_len = ctx->buf_size;

  return 0;
}

void hue_ent_cleanup(struct hue_ent_ctx *ctx)
{
  if (ctx->msg_buf)
  {
    free(ctx->msg_buf);
    ctx->msg_buf = NULL;
  }

  if (ctx->data)
  {
    free(ctx->data);
    ctx->data = NULL;
  }
}
