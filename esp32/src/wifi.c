#include "wifi.h"
#include "types.h"

// dummy list of SSIDs to compare against / simplest possible "friends list"
// really this is a hash table of "friends" and keys

typedef struct ssid {
  u8 name[33];
} key_t;

struct friend {
  key_t key;
  u8 friend_name[36];
  u8 wpa_key[64];
} friend_t;

#define ENTRY friend_t
#define KEY key_t
#include "hashtable.h"

key_t k;
friend_t *f;

hashtable_t *friends_table;
struct friend_t friends[256];

static struct friend_t me;

static struct wifi_ap_record_t ap_records[36];

u8 active_ssid[33];
u8 active_key[33];

struct wifi_ap_config_t ap_config;
struct wifi_sta_config_t sta_config;

uint16_t active_ap;
static uint16_t ap_count;

static struct wifi_init_config_t cfg;

void wifi_event_handler( void *ctx, wifi_event_t *event ) {
  switch ( event ) {
    case WIFI_EVENT_SCAN_DONE:
      ESP_ERROR_CHECK( esp_wifi_scan_get_ap_num( &ap_count );
      ESP_ERROR_CHECK( esp_wifi_scan_get_ap_records( &ap_count, ap_records ) );
      check_aps();
      break;
    case WIFI_EVENT_STA_CONNECTED:

      break;
    case WIFI_EVENT_STA_DISCONNECTED:

      break;
    default:
      break;
  }
}

int init_wifi ( EventGroupHandle_t wifi_event_group ) {

  me = {
    .key = {
      .name = "my ssid"
    },
    .friend_name = "nea999",
    .wpa_key = "neasPassword"
  };

  friends_table = friend_hashtable_new(36);

  friends[0] = { 
    .key = {
      .ssid = "TEST_NETWORK"
    },
    .friend_name = "test friend",
    .wpa_key = "testPassword"
  };
  friend_hashtable_add( friends_table, &friends[0] );

  k = { .ssid = "TEST_NETWORK" };

  f = friends_table_find( friends_table, k );

  strcpy(active_ssid, f.key.name);
  strcpy(active_key,  f.wpa_key);

  uint8_t *my_pass = hash_lookup( my_ssid );

  // placeholder, actually init these
  sta_config = { 
    .ssid = active_ssid,
    .password = active_key,
    .scan_method = WIFI_FAST_SCAN,
    .bssid_set = 0,
    .channel   = 0
  };

  ap_config = { 
    .ssid = me.key.name,
    .password = me.wpa_key,
    .ssid_len = strlen( my_ssid ),
    .channel  = 1,
    .authmode = WIFI_AUTH_WPA2_PSK,
    .ssid_hidden = 0,
    .max_connection = 10,
    .beacon_interval = 500 
  };

  ESP_ERROR_CHECK( esp_wifi_set_config( WIFI_IF_STA, &sta_config ) );
  ESP_ERROR_CHECK( esp_wifi_set_config( WIFI_IF_AP,  &ap_config  ) );

  tcpip_adapter_init();
  ESP_ERROR_CHECK( esp_event_loop_init(event_handler, wifi_event_group) );
  cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

  ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_FLASH) );
  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) );
}

void start_wifi( void ) {

}

void scan( void ) {
  ESP_ERROR_CHECK( esp_wifi_scan_start(&cfg, false));
}

int check_aps( void ) {
  struct key_t k;
  struct friend_t friend;
  for ( int i=0; i < ap_count; i++ ) {
    wifi_ap_record_t ap = ap_records[i];

    for ( int j=0; j < 48; j++ ) {
      strncpy(k.name, ap.ssid, 32);
      friend = friends_table_find( friends_table, &k );

      if ( strncmp( friend.key.name, ap.ssid, 32 ) == 0 ) {
        strncpy(sta_config.ssid, ap.ssid, 32);
        strncpy(sta_config.bssid, ap.bssid, 6);
        sta_config.channel = ap.primary;
        ap_config.authmode = ap.authmode;

        ESP_ERROR_CHECK( esp_wifi_set_config( WIFI_IF_STA, &sta_config );
        ESP_ERROR_CHECK( esp_wifi_connect() );
        // do stuff..
      } 
    }
  }

  return -1;
}

