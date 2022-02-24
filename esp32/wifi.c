#include "wifi.h"
#include "ht.h"

// dummy list of SSIDs to compare against / simplest possible "friends list"
// really this is a hash table of "friends" and keys
char ssids[48][48];

struct wifi_ap_config_t ap_config;
struct wifi_sta_config_t sta_config;

uint16_t active_ap;
static uint16_t ap_count;
static struct wifi_ap_record_t ap_records[24];

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

  uint8_t *pass = hash_lookup( ssids[0] );
  uint8_t *my_pass = hash_lookup( my_ssid );

  // placeholder, actually init these
  sta_config = { 
    .ssid = ssids[0],
    .password = pass,
    .scan_method = WIFI_FAST_SCAN,
    .bssid_set = 0,
    .channel   = 0
  };

  ap_config = { 
    .ssid = my_ssid,
    .password = my_pass,
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

// returns the index of found AP
int check_aps( void ) {
  for ( int i=0; i < ap_count; i++ ) {
    wifi_ap_record_t ap = ap_records[i];

    for ( int j=0; j < 48; j++ ) {
      if ( strcmp( ap.ssid, ssids[j] ) == 0 ) {
        handle_ap_found( i );
      } 
    }
  }

  return -1;
}

void handle_ap_found( int index ) {
  // lol
  wifi_ap_record_t ap = ap_records[index];

  strncpy(sta_config.ssid, ap.ssid, 32);
  strncpy(sta_config.bssid, ap.bssid, 6);
  // lookup shared secret pw
  sta_config.channel = ap.primary;
  ap_config.authmode = ap.authmode;
  // etc

  ESP_ERROR_CHECK( esp_wifi_set_config( WIFI_IF_STA, &sta_config );

  ESP_ERROR_CHECK( esp_wifi_connect() );

}
