#pragma once

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h> /* for htons */

struct hue_ent_message_data
{
  uint8_t   type;
  uint16_t  id;
  uint16_t  R;
  uint16_t  G;
  uint16_t  B;
} __attribute__((packed));


struct hue_ent_message_header
{
  char protocol_name[9];
  uint8_t version_major;
  uint8_t version_minor;
  uint8_t sequence_number; /* not used */
  uint8_t reserved1[2];
  uint8_t colour_space;    /* 0x00 = RGB; 0x01 = XY Brightness */
  uint8_t reserved2[1];
} __attribute__((packed));

struct hue_ent_ctx
{
  int light_count;
  struct hue_ent_message_header header;
  struct hue_ent_message_data   *data;
  int buf_size;
  void *msg_buf;
};


/* Function: hue_ent_init

   Initialise hue entertainment context. Be sure to call hue_ent_cleanup when finished with the context.

   Parameters:

      ctx - Context to initialise
      light_count - number of lights in entertainment area to be controlled

   Returns:

      0 on success, non-zero otherwise
*/
int hue_ent_init(struct hue_ent_ctx *ctx, int light_count);

/* Function: hue_ent_set_light_id

   Set the light id (as known to the hue bridge) for each light

   Parameters:

      ctx - hue_ent_ctx object
      index - light index (0 to light_count passed to hue_ent_init)
      hue_light_id - light id repoted by hue bridge

   Returns:

      0 on success, non-zero otherwise
*/
int hue_ent_set_light_id(struct hue_ent_ctx *ctx, int index, uint16_t hue_light_id);

/* Function: hue_ent_set_light

   Set light R/G/B values

   Parameters:

      ctx - hue_ent_ctx object
      index - light index (0 to light_count passed to hue_ent_init) to set
      R - Red value   (0 - 65,535)
      G - Green value (0 - 65,535)
      B - Blue value  (0 - 65,535)

   Returns:

      0 on success, non-zero otherwise
*/
int hue_ent_set_light(struct hue_ent_ctx *ctx, int index, uint16_t R, uint16_t G, uint16_t B);

/* Function: hue_ent_get_message

   Generate message to be sent to the hue bridge

   Parameters:

      ctx - hue_ent_ctx object
      out_msg_buf - (output) pointer to message buffer. Note that the buffers contents is overwritten on the next call to this function (do *not* free this buffer)
      out_buf_len - (outout) message size

   Returns:

      0 on success, non-zero otherwise
*/
int hue_ent_get_message(struct hue_ent_ctx *ctx, void **out_msg_buf, int *out_buf_len);

/* Function: hue_ent_cleanup

   Free any memory allocated when hue_ent_init was called.

   Parameters:

      ctx - hue_ent_ctx object
*/
void hue_ent_cleanup(struct hue_ent_ctx *ctx);
