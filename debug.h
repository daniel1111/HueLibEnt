#pragma once

typedef void (*debug_cb_t)(const char *message, void *user_data);

#define MSG_DEBUG  3
#define MSG_INFO   2
#define MSG_ERR    1
#define MSG_OFF    0
