#include <stdio.h>
#include <unistd.h>

#include "dtls.h"
#include "hue_entertainment.h"
#include "hue_rest.h"

static char *identity     = "nacVVb3raqYUKrxwtD80SzVYaTw2E43LovGQMTTw"; /* username, also used as psk identity */
static char *psk_key      = "758A46975E38CD62E991DC8AC4F170E9";
static char *ip_address   = "192.168.1.142";
static int  port_rest     = 443;
static int  port_dtls     = 2100;



void print_areas(struct hue_entertainment_area *ent_areas, int ent_areas_count)
{
  printf("\nEntertainment areas:\n");
  printf("ID\t%-*sLight IDs\n", AREA_NAME_LEN, "Name");
  if (ent_areas == NULL || ent_areas_count <= 0)
  {
    printf("*** None ***\n");
    return;
  }

  struct hue_entertainment_area *area_ptr;
  area_ptr = ent_areas;
  for (int i=0; i < ent_areas_count; i++)
  {
    /* Output ID / area name */
    printf("%d\t%-*s", area_ptr->area_id, AREA_NAME_LEN, area_ptr->area_name);

    /* Output list of light IDs */
    for (int j=0; j < MAX_LIGHTS_PER_AREA; j++)
      if (area_ptr->light_ids[j])
        printf("%d ", area_ptr->light_ids[j]);

    printf("\n");
    area_ptr++;
  }
  printf("\n");
}

void print_whitelist(struct hue_whitelist_entry *whitelist_entries, int whitelist_count)
{
  printf("\nWhitelist:\n");
  printf("Username\t\t\t\t\tCreated\t\t\tLast used\t\tName\n");
  if (whitelist_entries == NULL || whitelist_count <= 0)
  {
    /* Whitelist can't really be empty, becasue this app must be on it to be able to retreive it... */
    printf("*** None ***\n");
    return;
  }

  struct hue_whitelist_entry *wlist_ptr;
  wlist_ptr = whitelist_entries;
  for (int i=0; i < whitelist_count; i++)
  {
    printf("%s\t%s\t%s\t%s\n", wlist_ptr->username, wlist_ptr->created_date, wlist_ptr->last_use_date, wlist_ptr->name);
    wlist_ptr++;
  }
  printf("\n");
}

int main()
{
  int retval;
  struct dtls_ctx ctx_dtls;
  struct hue_ent_ctx ctx_ent;
  struct hue_rest_ctx ctx_hr;
  struct hue_entertainment_area *ent_areas;
  struct hue_whitelist_entry *whitelist_entries;
  uint whitelist_count;
  int ent_areas_count;


  hue_rest_init();
  hue_rest_init_ctx(&ctx_hr, NULL, ip_address, port_rest, identity);

  hue_rest_get_whitelist(&ctx_hr, &whitelist_entries, &whitelist_count);
  print_whitelist(whitelist_entries, whitelist_count);
//  hue_rest_cleanup_ctx(&ctx_hr);

  hue_rest_get_ent_groups(&ctx_hr, &ent_areas, &ent_areas_count);
  print_areas(ent_areas, ent_areas_count);

  if (ent_areas_count <= 0)
  {
    printf("No enterntertainment areas found... can't continue\n");
    hue_rest_cleanup_ctx(&ctx_hr);
    return -1;
  }

 // hue_rest_delete_user(&ctx_hr, "9Q2X3XNfPVQfDf2rmZ82rLwrYv6EMoNFWOww49Kw");
 // hue_rest_delete_user(&ctx_hr, "kekHXo0cPvcaovLYL48k5XZbrxAy5MirHdzflJsP");

  char *out_username;
  char *out_clientkey;
//  hue_rest_register(&ctx_hr, &out_username, &out_clientkey);
//  printf("username = %s\nclientkey = %s\n\n", out_username, out_clientkey);


   hue_rest_cleanup_ctx(&ctx_hr);
 return 0;

  /* Use the first enterntertainment area found */
  hue_rest_activate_stream(&ctx_hr, ent_areas->area_id);


  hue_ent_init(&ctx_ent, 1);
  hue_ent_set_light_id(&ctx_ent, 0, ent_areas->light_ids[0]); /* use first light in area */
  //hue_ent_set_light_id(&ctx_ent, 1, 12);
  //hue_ent_set_light_id(&ctx_ent, 2, 13);
  //hue_ent_set_light_id(&ctx_ent, 3, 14);

  dtls_init(&ctx_dtls, identity, psk_key, NULL);
  retval = dtls_connect(&ctx_dtls, ip_address, port_dtls);
  printf("dtls_connect ret = %d\n", retval);
  if (retval)
    exit(-1);

  while (1)
  {
    void *msg_buf;
    int buf_len;
    static uint16_t counter=0;
    usleep(100000);

  //  for (int i=0; i < 4; i++)

    printf("count=%d\n", counter);
    hue_ent_set_light(&ctx_ent, 0, 0, 0, counter);

    counter += 100;

    hue_ent_get_message(&ctx_ent, &msg_buf, &buf_len);

    if (dtls_send_data(&ctx_dtls, msg_buf, buf_len))
      return -1;
  }

}
