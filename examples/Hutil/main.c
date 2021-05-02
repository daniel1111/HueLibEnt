#include <stdio.h>
#include <unistd.h>
#include <libconfig.h>

#include "hue_dtls.h"
#include "hue_entertainment.h"
#include "hue_rest.h"

#define VERSION "0.1"

#define LEN_USERNAME 100
#define LEN_PSK      100
#define LEN_IP       20

#define DTLS_PORT 2100
#define SSL_PORT  443

int debug_level = HUE_MSG_ERR;


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
    hue_rest_init_ctx(&ctx_hr, NULL, cmdline_ipaddress, SSL_PORT, "", HUE_MSG_ERR);
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

void print_usage(const char* name)
{
  printf("\nHue utility %s\n", VERSION);
  printf("Usage: %s [options]\n", name);
  printf("Misc Hue utility functions\n\n");

  printf("Options:\n");
  printf("    -d <level>                Debug level 0-3. Default: 1 (errors only)\n");
  printf("    -e                        List entertainment areas\n");
  printf("    -p <credentials file>     Use <credentials file>\n");
  printf("    -r <ip address>           Register with bridge at <ip address>. On success, (over)writes bridge_credentials.conf\n");
  printf("    -w                        Show whitelist\n");
/*printf("    -x                        Remove whitelist entry\n"); - currently broken - see https://developers.meethue.com/forum/t/delete-whitelist-entry-no-longer-works/6097 */
  printf("    -h                        This help\n");
  printf("\n");
}


int main (int argc, char **argv)
{
  int c;
  const char *default_credentials_filename = "bridge_credentials.conf";
  char *cmdline_credentials_file = NULL;
  char *cmdline_ipaddress = NULL;
  char connection_username[LEN_USERNAME] = "";
  char connection_psk[LEN_PSK] = "";
  char connection_ip[LEN_IP] = "";

  int retval;
  struct hue_dtls_ctx ctx_dtls;
  struct hue_ent_ctx ctx_ent;
  struct hue_rest_ctx ctx_hr;
  struct hue_entertainment_area *ent_areas;
  struct hue_whitelist_entry *whitelist_entries;
  uint whitelist_count;
  int ent_areas_count;
  int show_whitelist = 0;
  int show_ent_areas = 0;
  const char *username_to_delete = NULL;
  
  while ((c = getopt (argc, argv, "d:ep:r:wx:hH")) != -1)
  {
    switch (c)
      {
      case 'd': /* Debug level */
        debug_level = atoi(optarg);
        break;

      case 'e': /* Show enterntertainment areas */
        show_ent_areas = 1;
        break;

      case 'p':
        cmdline_credentials_file = optarg;
        break;

      case 'r': /* register */
        cmdline_ipaddress = optarg;
        break;

      case 'w': /* show whitelist */
        show_whitelist = 1;
        break;

      case 'x': /* Remove whitelist entry */
        username_to_delete = optarg;
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

  if (!(show_whitelist || show_ent_areas || username_to_delete || cmdline_ipaddress))
  {
    print_usage(argv[0]);
    return -1;
  }

  if (get_bridge_credentials(argv[0], (cmdline_credentials_file ? cmdline_credentials_file : default_credentials_filename), cmdline_ipaddress, connection_username, connection_psk, connection_ip))
  {
    printf("Failed to get credentials to connect to bridge\n");
    return -1;
  }

  hue_rest_init();
  hue_rest_init_ctx(&ctx_hr, NULL, connection_ip, SSL_PORT, connection_username, debug_level);

  if (show_whitelist)
  {
    hue_rest_get_whitelist(&ctx_hr, &whitelist_entries, &whitelist_count);
    print_whitelist(whitelist_entries, whitelist_count);
  }

  if (show_ent_areas)
  {
    hue_rest_get_ent_groups(&ctx_hr, &ent_areas, &ent_areas_count);
    print_areas(ent_areas, ent_areas_count);

    if (ent_areas_count <= 0)
      printf("No enterntertainment areas found...\n");
  }

  if (username_to_delete)
  {
    printf("Removing [%s] from whitelist\n", username_to_delete);
    hue_rest_delete_user(&ctx_hr, username_to_delete);
  }

  hue_rest_cleanup_ctx(&ctx_hr);
  return 0;
}
