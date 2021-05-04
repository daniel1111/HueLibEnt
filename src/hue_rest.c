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

#include "hue_rest.h"

#include <curl/curl.h>

#include <string.h>
#include <stdlib.h>

enum req_type { REQTYPE_GET, REQTYPE_PUT, REQTYPE_POST, REQTYPE_DELETE };

void hue_debug(struct hue_rest_ctx *ctx, int level, char *fmt, ...)
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
      printf("[hue_rest] ");
      vprintf(fmt, args);
      puts("");
    }
  }
  va_end(args);
}

void free_if_not_null(void **ptr)
{
  if (*ptr)
  {
    free(*ptr);
    *ptr = NULL;
  }
}

int compare_version_string(const char *version_needed, const char *version_found)
{
  unsigned version_major_needed = 0;
  unsigned version_minor_needed = 0;
  unsigned version_patch_needed = 0;

  unsigned version_major_found = 0;
  unsigned version_minor_found = 0;
  unsigned version_patch_found = 0;

  sscanf(version_needed, "%u.%u.%u", &version_major_needed, &version_minor_needed, &version_patch_needed);
  sscanf(version_found, "%u.%u.%u", &version_major_found, &version_minor_found, &version_patch_found);

  if (version_major_needed < version_major_found)
    return 1;
  if (version_major_needed > version_major_found)
    return -1;

  if (version_minor_needed < version_minor_found)
    return 1;
  if (version_minor_needed > version_minor_found)
    return -1;

  if (version_patch_needed < version_patch_found)
    return 1;
  if (version_patch_needed > version_patch_found)
    return -1;

  return 0;
}

int hue_rest_init()
{
  return curl_global_init(CURL_GLOBAL_DEFAULT);
}

void hue_rest_cleanup()
{
  curl_global_cleanup();
}

static void free_whitelist(struct hue_rest_ctx *ctx)
{
  if (ctx->whitelist == NULL || ctx->whitelist_count == 0)
    return; /* Nothing to do, just exit */

  for (uint i=0; i < ctx->whitelist_count; i++)
  {
    struct hue_whitelist_entry *cur = ctx->whitelist+i;
    if (cur->username)      free(cur->username);
    if (cur->last_use_date) free(cur->last_use_date);
    if (cur->created_date)  free(cur->created_date);
    if (cur->name)          free(cur->name);
  }

  free(ctx->whitelist);
  ctx->whitelist = NULL;
  ctx->whitelist_count = 0;
}

int hue_rest_init_ctx(struct hue_rest_ctx *ctx, hue_debug_cb_t debug_callback, const char *address, int port, const char *username, int debug_level)
{
  int len;

  memset(ctx, 0, sizeof(struct hue_rest_ctx));
  if (debug_callback)
    ctx->debug_callback = debug_callback;

  ctx->debug_level = debug_level;

  /* store username */
  len = strlen(username)+1;
  if (!(ctx->username = malloc(len)))
    return -1;
  strcpy(ctx->username, username);

  /* store address */
  len = strlen(address)+1;
  if (!(ctx->address = malloc(len)))
    return -1;
  strcpy(ctx->address, address);
  ctx->port = port;

  ctx->ent_areas = NULL;

  return 0;
}

void hue_rest_cleanup_ctx(struct hue_rest_ctx *ctx)
{
  free_if_not_null((void **)&ctx->username);
  free_if_not_null((void **)&ctx->address);
  free_if_not_null((void **)&ctx->received_data);
  free_if_not_null((void **)&ctx->upload_data);
  free_if_not_null((void **)&ctx->ent_areas);
  free_if_not_null((void **)&ctx->apiversion);
  free_if_not_null((void **)&ctx->clientkey);

  free_whitelist(ctx);
}

/* Called by cURL for PUT requests to get the data we want to send */
static size_t curl_read_cb(void *ptr, size_t size, size_t nmemb, void *stream)
{
  struct hue_rest_ctx *ctx;
  size_t max_size;
  size_t bytes_to_copy;
  ctx = stream;
  max_size = size * nmemb;

  /* Ideally, we want to copy all of ctx->upload_data into ptr. But check there's space first */
  if (ctx->upload_data_length > max_size)
    bytes_to_copy = max_size;
  else
    bytes_to_copy = ctx->upload_data_length;

  memcpy(ptr, ctx->upload_data, bytes_to_copy);
  hue_debug(ctx, HUE_MSG_INFO, " > %.*s", bytes_to_copy, ptr);

  return bytes_to_copy;
}

static int curl_trace_cb(CURL *handle, curl_infotype type, char *data, size_t size, void *userp)
{
  struct hue_rest_ctx *ctx = userp;

  (void)handle;    /* prevent compiler warning */

  if (type == CURLINFO_TEXT)
  {
    /* remove \n, as hue_debug() adds it */
    if (size > 2)
      data[size-1] = '\0';
    hue_debug(ctx, HUE_MSG_DEBUG, "curl: %s", data);
  }
  return 0;
}

/* This is called by cURL when we receive data */
static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
  struct hue_rest_ctx *ctx = userp;
  size_t realsize = size * nmemb;

  hue_debug(ctx, HUE_MSG_DEBUG, "curl_write_cb> recieved %d bytes", size * nmemb);

  char *ptr = realloc(ctx->received_data, ctx->received_data_length + realsize + 1);
  if(ptr == NULL)
  {
    /* out of memory! */
    hue_debug(ctx, HUE_MSG_ERR, "curl_write_cb> not enough memory (realloc returned NULL)\n");
    return 0;
  }

  ctx->received_data = ptr;
  memcpy(&(ctx->received_data[ctx->received_data_length]), contents, realsize);
  ctx->received_data_length += realsize;
  ctx->received_data[ctx->received_data_length] = 0;

  return nmemb;
}

static int configure_curl(struct hue_rest_ctx *ctx, enum req_type rt, const char *url)
{
  CURLcode res;
  CURL *curl = ctx->curl;
  curl = curl_easy_init();
  if (ctx->received_data)
    free(ctx->received_data);

  ctx->received_data = malloc(1);   /* will be grown as needed by the realloc in the write callback */
  ctx->received_data_length = 0;    /* no data at this point */ 

  if (curl)
  {
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_trace_cb);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, ctx);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);

    /* The bridge's certificate won't have been signed by a CA we recognise, or have a cn that matches the address */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);

    /* we want to use our own read function */
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, curl_read_cb);

    /* wait a maximum of 10 seconds before giving up */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    if (rt == REQTYPE_PUT)
    {
      /* enable uploading */
      curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

      /* HTTP PUT please */
      curl_easy_setopt(curl, CURLOPT_PUT, 1L);

      /* now specify which file to upload */
      curl_easy_setopt(curl, CURLOPT_READDATA, ctx);

      /* provide the size of the upload, we specicially typecast the value
         to curl_off_t since we must be sure to use the correct data size */
      curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)ctx->upload_data_length);
    }
    else if (rt == REQTYPE_DELETE)
    {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    else if (rt == REQTYPE_POST)
    {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ctx->upload_data);
    }

    /* specify target URL, and note that this URL should include a file
       name, not only a directory */
    curl_easy_setopt(curl, CURLOPT_URL, url);

    /* Now run off and do what you've been told! */
    res = curl_easy_perform(curl);

    /* Check for errors */
    if (res != CURLE_OK)
      hue_debug(ctx, HUE_MSG_ERR, "curl_easy_perform() failed: %s", curl_easy_strerror(res));
    else
      hue_debug(ctx, HUE_MSG_INFO, " < %.*s", ctx->received_data_length, ctx->received_data);

    /* always cleanup */
    curl_easy_cleanup(curl);

    return (res == CURLE_OK ? 0 : -1);
  }
  else
  {
    hue_debug(ctx, HUE_MSG_ERR, "curl_easy_init() failed");
    return -1;
  }
}

int hue_rest_activate_stream(struct hue_rest_ctx *ctx, int group)
{
  char url[254];
  int retval;
  const char *activate_stream = "{\"stream\":{\"active\":true}}";

  ctx->upload_data_length = strlen(activate_stream)+1;
  if (!(ctx->upload_data = malloc(ctx->upload_data_length)))
    return -1;
  strcpy(ctx->upload_data, activate_stream);

  /* build up URL */
  snprintf(url, sizeof(url), "https://%s:%d/api/%s/groups/%d",
           ctx->address, ctx->port, ctx->username, group);
  url[sizeof(url)-1] = '\0';
  hue_debug(ctx, HUE_MSG_INFO, "URL = %s", url);

  /* make PUT request */
  retval = configure_curl(ctx, REQTYPE_PUT, url);

  if (ctx->upload_data)
  {
    free(ctx->upload_data);
    ctx->upload_data = NULL;
    ctx->upload_data_length = 0;
  }

  return retval;
}

/* Returns:
 * -1 Parse error (or other "bad" error)
 *  0 Not an error message
 *  1 Error messsage - out_type set
 */
static int parse_error_message(struct hue_rest_ctx *ctx, const char* msg, int *out_type)
{
  char *error_msg;

  hue_debug(ctx, HUE_MSG_DEBUG, "parse_error_message> %s", msg);

  json_object *jobj = json_tokener_parse(msg);
  if (jobj == NULL)
  {
    hue_debug(ctx, HUE_MSG_ERR, "Failed to parse JSON received: %s", msg);
    return -1;
  }
  *out_type = 0;

  if (!json_object_is_type(jobj, json_type_array))
  {
     hue_debug(ctx, HUE_MSG_DEBUG, "Not an error message (not an array)");
     json_object_put(jobj);
     return 0;
  }

  if (json_object_array_length(jobj) <= 0)
  {
    hue_debug(ctx, HUE_MSG_DEBUG, "Not an error");
    json_object_put(jobj);
    return 0;
  }

  json_object *obj = json_object_array_get_idx(jobj, 0);

  json_object *obj_param = NULL;
  json_object_object_get_ex(obj, "error", &obj_param);
  if (obj_param == NULL)
  {
    hue_debug(ctx, HUE_MSG_DEBUG, "Not an error message");
    json_object_put(jobj);
    return 0;
  }

  error_msg = malloc(strlen(json_object_get_string(obj_param)) + 1);
  if (error_msg == NULL)
  {
    hue_debug(ctx, HUE_MSG_ERR, "parse_error_message> Failed to allocate memory!");
    json_object_put(jobj);
    return -1;
  }
  strcpy(error_msg, json_object_get_string(obj_param));

  json_object_put(jobj);
  jobj = json_tokener_parse(error_msg);

  json_object_object_get_ex(jobj, "type", &obj_param);

  if (obj_param == NULL)
  {
    hue_debug(ctx, HUE_MSG_ERR, "Failed to find type in error message");
    json_object_put(jobj);
    free(error_msg);
    return -1;
  }

  *out_type = json_object_get_int(obj_param);
  hue_debug(ctx, HUE_MSG_DEBUG, "error type: %d", *out_type);
  json_object_put(jobj);
  free(error_msg);

  return 1;
}

/* If not null, free the return value when finished with it.
 * 
 * Expects to find a JSON config message from the bridge in
 * ctx->received_data, and returns the "whitelist" part
 * (still JSON).
 */
static char* extract_json_whitelist_from_config(struct hue_rest_ctx *ctx)
{
  char *retval = NULL;
  struct json_object_iterator it;
  struct json_object_iterator itEnd;

  json_object *jobj = json_tokener_parse(ctx->received_data);
  if (jobj == NULL)
  {
    hue_debug(ctx, HUE_MSG_ERR, "Failed to parse JSON received: %s", ctx->received_data);
    return NULL;
  }

  if (json_object_get_type(jobj) !=  json_type_object)
  {
    hue_debug(ctx, HUE_MSG_INFO, "Unexpected JSON received");
    json_object_put(jobj);
    return NULL;
  }

  it = json_object_iter_begin(jobj);
  itEnd = json_object_iter_end(jobj);
  while (!json_object_iter_equal(&it, &itEnd))
  {
    if (!strcmp("whitelist", json_object_iter_peek_name(&it)))
    {
      const char *whitelist_json = json_object_get_string(json_object_iter_peek_value(&it));
      retval = malloc(strlen(whitelist_json)+1);
      strcpy(retval, whitelist_json);
      json_object_put(jobj);
      return retval;
    }
    json_object_iter_next(&it);
  }

  json_object_put(jobj);
  return NULL;
}

/* For a JSON string with key/value pairs, extract the value given a key.
 * On failure, NULL is returned, otherwise the return value is a pointer
 * to the value, which must be free'd when finished with.
 */
static char *get_value_from_jobj(struct hue_rest_ctx *ctx, json_object *jobj, const char *key)
{
  char *value = NULL;

  json_object *obj_param = NULL;
  if (!json_object_object_get_ex(jobj, key, &obj_param))
  {
    hue_debug(ctx, HUE_MSG_ERR, "get_value_from_jobj> Failed to get [%s] from JSON", key);
    return NULL;
  }

  const char *param_val = json_object_get_string(obj_param);
  if (param_val == NULL)
  {
    hue_debug(ctx, HUE_MSG_ERR, "get_value_from_jobj> failed to get value for [%s]", key);
    return NULL;
  }

  value = calloc(strlen(param_val)+1, 1);
  if (value)
    strcpy(value, param_val);
  else
    hue_debug(ctx, HUE_MSG_ERR, "get_value_from_jobj> failed to allocate memory!");

  return value;
}

// {"name":"Hue Bridge","datastoreversion":"99","swversion":"1943185030","apiversion":"1.43.0","mac":"00:17:88:2d:30:81","bridgeid":"001788FFFE2D3081","factorynew":false,"replacesbridgeid":null,"modelid":"BSB002","starterkitid":""}
static int parse_unauth_configuration_response_json(struct hue_rest_ctx *ctx)
{
  const char *apiversion;
  hue_debug(ctx, HUE_MSG_DEBUG, "parse_unauth_configuration_response> %s", ctx->received_data);

  json_object *jobj = json_tokener_parse(ctx->received_data);
  if (jobj == NULL)
  {
    hue_debug(ctx, HUE_MSG_ERR, "parse_unauth_configuration_response> Failed to parse JSON received: %s", ctx->received_data);
    return -1;
  }

  free_if_not_null((void **)&ctx->apiversion);
  ctx->apiversion = get_value_from_jobj(ctx, jobj, "apiversion");
  json_object_put(jobj);

  return 0;
}

// [{"success":{"username":"xM7Kno9zv7J8vIiM0rTjPU1NJsMjJpafXG4q8yqQ","clientkey":"B95676C8F5E21AEAD54E5D8A38844A21"}}]
static int parse_register_response(struct hue_rest_ctx *ctx)
{
  hue_debug(ctx, HUE_MSG_DEBUG, "parse_register_response> %s", ctx->received_data);

  json_object *jobj = json_tokener_parse(ctx->received_data);
  if (jobj == NULL)
  {
    hue_debug(ctx, HUE_MSG_ERR, "parse_register_response> Failed to parse JSON received: %s", ctx->received_data);
    return -1;
  }

  json_object *obj = json_object_array_get_idx(jobj, 0);
  // TODO: obj type check
  json_object *obj_param = NULL;
  json_object_object_get_ex(obj, "success", &obj_param);
  if (obj_param == NULL)
  {
    hue_debug(ctx, HUE_MSG_ERR, "parse_register_response> Not an success message");
    json_object_put(jobj);
    return -1;
  }

  free_if_not_null((void **)&ctx->username);
  free_if_not_null((void **)&ctx->clientkey);
  ctx->username  = get_value_from_jobj(ctx, obj_param, "username");
  ctx->clientkey = get_value_from_jobj(ctx, obj_param, "clientkey");
  json_object_put(jobj);

  return 0;
}

/* Populate entry_ptr with username, and last use date / created date / name extracted from data.
 * Note that memory is allocted for entry_ptr->last_use_date, ->created_date, etc. so must be free'd
 * when finished with (dealt with by free_whitelist())
 */
static int add_whitelist_entry(struct hue_rest_ctx *ctx, struct hue_whitelist_entry *entry_ptr, const char *username, const char *data)
{
  /* Extract last use date, created date & name from data and save into entry_ptr */

  json_object *jobj = json_tokener_parse(data);
  if (jobj == NULL)
  {
    hue_debug(ctx, HUE_MSG_ERR, "Failed to parse JSON: %s", data);
    return -1;
  }

  entry_ptr->last_use_date = get_value_from_jobj(ctx, jobj, "last use date");
  entry_ptr->created_date  = get_value_from_jobj(ctx, jobj, "create date");
  entry_ptr->name          = get_value_from_jobj(ctx, jobj, "name");

  if ((entry_ptr->username = calloc(strlen(username)+1, 1)))
    strcpy(entry_ptr->username, username);

  json_object_put(jobj);
  return 0;
}

/* Extract the app whitelist from the passed in whitelist extracted from a config api request,
 * and store in ctx->whitelist
 */
static int extract_json_entries_from_whitelist(struct hue_rest_ctx *ctx, char *json_whitelist)
{
  struct json_object_iterator it;
  struct json_object_iterator itEnd;

  free_whitelist(ctx);

  if (json_whitelist == NULL)
  {
    hue_debug(ctx, HUE_MSG_INFO, "Empty/NULL whitelist!");
    return -1;
  }

  json_object *jobj = json_tokener_parse(json_whitelist);
  if (jobj == NULL)
  {
    hue_debug(ctx, HUE_MSG_ERR, "Failed to parse JSON whitelist: %s", json_whitelist);
    return -1;
  }

  it = json_object_iter_begin(jobj);
  itEnd = json_object_iter_end(jobj);
  while (!json_object_iter_equal(&it, &itEnd))
  {
    ctx->whitelist_count++;
    json_object_iter_next(&it);
  }

  ctx->whitelist = calloc(sizeof(struct hue_whitelist_entry), ctx->whitelist_count);

  it = json_object_iter_begin(jobj);
  itEnd = json_object_iter_end(jobj);
  struct hue_whitelist_entry *entry_ptr = ctx->whitelist;
  while (!json_object_iter_equal(&it, &itEnd))
  {
    add_whitelist_entry(ctx, entry_ptr, json_object_iter_peek_name(&it), json_object_get_string(json_object_iter_peek_value(&it)));
    entry_ptr++;
    json_object_iter_next(&it);
  }

  json_object_put(jobj);
  return 0;
}

/* Extract entertainement areas from "/groups" api request, and return in out_areas/out_areas_count.
 * Note: On success, memory is allocated for out_areas, this must be free'd by the caller.
 */
static int parse_entertainment_groups_json(struct hue_rest_ctx *ctx, struct hue_entertainment_area **out_areas, int *out_areas_count)
{
  int retval = 0;
  int error_type;
  struct json_object_iterator it;
  struct json_object_iterator itEnd;
  struct hue_entertainment_area *areas;
  *out_areas_count = 0;

  json_object *jobj = json_tokener_parse(ctx->received_data);
  if (jobj == NULL)
  {
    hue_debug(ctx, HUE_MSG_DEBUG, "Failed to parse JSON received: %s", ctx->received_data);
    return -1;
  }

  if ((retval = parse_error_message(ctx, ctx->received_data, &error_type)))
  {
    if (retval < 0)
    {
      /* Parse error, or something bad */
      hue_debug(ctx, HUE_MSG_ERR, "Failed to parse groups response");
    }
    else
    {
      /* Error has been reported by the bridge */
      if (error_type == HUE_ERR_UNAUTHORIZED)
        hue_debug(ctx, HUE_MSG_ERR, "Get groups failed: Unauthorized."); /* Link button on the bridge hasn't been pressed in the last 30s */
      else
        hue_debug(ctx, HUE_MSG_ERR, "Get groups failed: Unexpected error type (%d) received from bridge", error_type);

      retval = error_type;
    }
    json_object_put(jobj);
    return retval;
  }

  if (json_object_get_type(jobj) !=  json_type_object)
  {
    hue_debug(ctx, HUE_MSG_DEBUG, "Unexpected JSON received");
    json_object_put(jobj);
    return -1;
  }

  /* Count the number of entertainment areas returned (probably only ever one?) */
  it = json_object_iter_begin(jobj);
  itEnd = json_object_iter_end(jobj);
  while (!json_object_iter_equal(&it, &itEnd))
  {
    json_object *obj_param = NULL;
    json_object_object_get_ex(json_object_iter_peek_value (&it), "type", &obj_param);

    if (!strcmp("Entertainment", json_object_get_string(obj_param)))
      (*out_areas_count)++;
    json_object_iter_next(&it);
  }

  hue_debug(ctx, HUE_MSG_DEBUG, "found %d ent. area(s)", *out_areas_count);

  /* Allocate memory for areas */
  *out_areas = malloc(*out_areas_count * sizeof(struct hue_entertainment_area));
  memset(*out_areas, 0, *out_areas_count * sizeof(struct hue_entertainment_area));
  areas = *out_areas;

  /* Add areas found with ligts to out_areas */
  it = json_object_iter_begin(jobj);
  itEnd = json_object_iter_end(jobj);

  while (!json_object_iter_equal(&it, &itEnd))
  {
    const char *area_name;
    struct json_object *j1;
    j1 = json_object_iter_peek_value (&it);
    json_object *obj_param = NULL;
    json_object_object_get_ex(j1, "type", &obj_param);

    /* Only interested in "Entertainment" type groups */
    if (!strcmp("Entertainment", json_object_get_string(obj_param)))
    {
      /* Get area name */
      json_object_object_get_ex(j1, "name", &obj_param);
      area_name = json_object_get_string(obj_param);
      strncpy((*areas).area_name, area_name, AREA_NAME_LEN);

      /* Get area id */
      (*areas).area_id = atoi(json_object_iter_peek_name(&it));

      /* Get light IDs */
      json_object_object_get_ex(j1, "lights", &obj_param);
      area_name = json_object_get_string(obj_param);

      for (int n = 0; n < json_object_array_length(obj_param) && n < MAX_LIGHTS_PER_AREA; n++)
      {
        json_object *obj = json_object_array_get_idx(obj_param, n);
        (*areas).light_ids[n] = json_object_get_int(obj);
      }
      areas++;
    }

    json_object_iter_next(&it);
  }

  json_object_put(jobj);
  return 0;
}

int hue_rest_get_ent_groups(struct hue_rest_ctx *ctx, struct hue_entertainment_area **out_areas, int *out_areas_count)
{
  char url[254];
  int retval;
  *out_areas_count = 0;
  *out_areas = NULL;

  /* build up URL */
  snprintf(url, sizeof(url), "https://%s:%d/api/%s/groups",
           ctx->address, ctx->port, ctx->username);
  url[sizeof(url)-1] = '\0';
  hue_debug(ctx, HUE_MSG_INFO, "URL = %s", url);

  /* make GET request */
  retval = configure_curl(ctx, REQTYPE_GET, url);

  if (ctx->ent_areas)
  {
    free(ctx->ent_areas);
    ctx->ent_areas = NULL;
  }

  if (retval)
  {
    hue_debug(ctx, HUE_MSG_DEBUG, "GET request failed.", url);
    return -1;
  }

  if (parse_entertainment_groups_json(ctx, out_areas, out_areas_count))
  {
    hue_debug(ctx, HUE_MSG_DEBUG, "Failed to get entertainment group", url);
    return -1;
  }

  /* Keep track of memory so it can be freeded in hue_rest_cleanup_ctx */
  ctx->ent_areas = *out_areas;

  return retval;
}

/* Get the bridge config, and if successfull, populate ctx->whitelist/whitelist_count. */
static int get_config(struct hue_rest_ctx *ctx)
{
  char url[254];
  int retval;

  free_whitelist(ctx);

  /* build up URL */
  snprintf(url, sizeof(url), "https://%s:%d/api/%s/config",
           ctx->address, ctx->port, ctx->username);
  url[sizeof(url)-1] = '\0';
  hue_debug(ctx, HUE_MSG_INFO, "URL = %s", url);

  /* make GET request */
  retval = configure_curl(ctx, REQTYPE_GET, url);


  if (retval)
  {
    hue_debug(ctx, HUE_MSG_DEBUG, "GET request failed.", url);
    return -1;
  }

  char *whitelist_json = extract_json_whitelist_from_config(ctx);
  extract_json_entries_from_whitelist(ctx, whitelist_json);
  free(whitelist_json);

  return retval;
}

int hue_rest_get_whitelist(struct hue_rest_ctx *ctx, struct hue_whitelist_entry **out_whitelist_entries, uint *out_whitelist_count)
{
  if (get_config(ctx))
    return -1;

  *out_whitelist_entries = ctx->whitelist;
  *out_whitelist_count = ctx->whitelist_count;

  return 0;
}

static char *get_device_type(struct hue_rest_ctx *ctx)
{
  extern char *__progname;
  /* app name (1..20) # device name (1..19) */
  char app_name [HUE_APP_NAME_SIZE];
  char device_name[HUE_DEVICE_NAME_SIZE];

  snprintf(app_name, sizeof(app_name), "%s-%s", "LibHueEnt", __progname);
  app_name[sizeof(app_name)-1] = '\0';

  gethostname(device_name, sizeof(device_name));
  device_name[sizeof(device_name)-1] = '\0';

  snprintf(ctx->devicetype, (sizeof(ctx->devicetype) > 40 ? 40 : sizeof(ctx->devicetype)), "%s#%s", app_name, device_name);
  return ctx->devicetype;
}


int hue_rest_delete_user(struct hue_rest_ctx *ctx, const char *username)
{
  char url[254];
  int retval;

  /* build up URL */
  snprintf(url, sizeof(url), "https://%s:%d/api/%s/config/whitelist/%s",
           ctx->address, ctx->port, ctx->username, username);
  url[sizeof(url)-1] = '\0';
  hue_debug(ctx, HUE_MSG_INFO, "URL = %s", url);

  /* make DELETE request */
  retval = configure_curl(ctx, REQTYPE_DELETE, url);


  if (retval)
  {
    hue_debug(ctx, HUE_MSG_ERR, "Delete request failed.", url);
    return -1;
  }

  return retval;
}

int get_unauth_config(struct hue_rest_ctx *ctx)
{
  int retval;
  char url[254];

  /* build up URL*/
  snprintf(url, sizeof(url), "https://%s:%d/api/config", ctx->address, ctx->port);
  url[sizeof(url)-1] = '\0';
  hue_debug(ctx, HUE_MSG_INFO, "URL = %s", url);


  /* make GET request */
  retval = configure_curl(ctx, REQTYPE_GET, url);

  if (ctx->apiversion)
    ctx->apiversion = NULL;

  if (retval)
  {
    hue_debug(ctx, HUE_MSG_DEBUG, "GET request failed.", url);
    return -1;
  }

  if (parse_unauth_configuration_response_json(ctx))
  {
    hue_debug(ctx, HUE_MSG_DEBUG, "Failed to get unauth config", url);
    return -1;
  }

  return retval;
}

int hue_rest_validate_apiversion(struct hue_rest_ctx *ctx)
{
  int retval;
  char api_version_needed[] = HUE_ENTERTAINMENT_API_NEEDED;

  if(get_unauth_config(ctx))
  {
    hue_debug(ctx, HUE_MSG_DEBUG, "Failed to compare version string", NULL);
    return -1;
  }

  retval = compare_version_string(api_version_needed, ctx->apiversion);
  if (retval < 0) {
    hue_debug(ctx, HUE_MSG_INFO, "api version found (%s) is to low. must be >= 1.22.0", ctx->apiversion);
    return -1;
  }
  hue_debug(ctx, HUE_MSG_DEBUG, "api version found (%s) is compatible", ctx->apiversion);

  return 0;
}

int hue_rest_register(struct hue_rest_ctx *ctx, char **out_username, char **out_clientkey)
{
  char url[254];
  int retval;
  const char *devicetype;
  int error_type;

  *out_username  = NULL;
  *out_clientkey = NULL;

  /* build up URL */
  snprintf(url, sizeof(url), "https://%s:%d/api", ctx->address, ctx->port);
  url[sizeof(url)-1] = '\0';
  hue_debug(ctx, HUE_MSG_INFO, "URL = %s", url);

  /* Generate payload */
  devicetype = get_device_type(ctx);
  ctx->upload_data_length = snprintf(NULL, 0, "{\"devicetype\":\"%s\",\"generateclientkey\":true}", devicetype)+1;
  ctx->upload_data = calloc(ctx->upload_data_length+1, 1);
  snprintf(ctx->upload_data, ctx->upload_data_length, "{\"devicetype\":\"%s\",\"generateclientkey\":true}", devicetype);
  hue_debug(ctx, HUE_MSG_INFO, "Body = %s", ctx->upload_data);

  /* make POST request */
  retval = configure_curl(ctx, REQTYPE_POST, url);

  if (ctx->upload_data)
  {
    free(ctx->upload_data);
    ctx->upload_data = NULL;
    ctx->upload_data_length = 0;
  }

  if (retval)
  {
    hue_debug(ctx, HUE_MSG_ERR, "Register failed.", url);
    return -1;
  }

  if ((retval = parse_error_message(ctx, ctx->received_data, &error_type)))
  {
    if (retval < 0)
    {
      /* Parse error, or something bad */
      hue_debug(ctx, HUE_MSG_ERR, "Register failed", url);
    }
    else
    {
      /* Error has been reported by the bridge */
      if (error_type == HUE_ERR_LINK_BUTTON_NOT_PUSHED)
        hue_debug(ctx, HUE_MSG_ERR, "Register failed: Link button on the bridge not pressed within last 30 seconds"); /* Link button on the bridge hasn't been pressed in the last 30s */
      else
        hue_debug(ctx, HUE_MSG_ERR, "Register failed: Unexpected error type (%d) received from bridge", error_type); /* ??? Why else is registration likely to fail? */

      retval = error_type;
    }
  }
  else
  {
    /* Not an error message, so assume it worked */
    if (parse_register_response(ctx))
      return -2;
    else
    {
      *out_username  = ctx->username;
      *out_clientkey = ctx->clientkey;
      retval = 0;
    }
  }

  return retval;
}
