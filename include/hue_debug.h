#pragma once

typedef void (*hue_debug_cb_t)(const char *message, void *user_data);

#define HUE_MSG_DEBUG  3
#define HUE_MSG_INFO   2
#define HUE_MSG_ERR    1
#define HUE_MSG_OFF    0
