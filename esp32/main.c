#include "wifi.h"
#include "friends.h"

#define DUMMY_FRIENDS_MAX 64 

struct ht_table *mac_ip_table;

enum {
  WIFI_EVENT_DISCONNECTED = 1 << 0,
  WIFI_EVENT_CONNECTED = 1 << 1,
};

friend_t friends_list[DUMMY_FRIENDS_MAX];

uint8_t friends_list[NUM_FRIEND_IDS][33]
wifi_ap_record_t access_points[20];
TaskHandle_t wifi_task_handle;
TimerHandle_t reconnect_timer;
EventGroupHandle_t wifi_event_group;

typedef enum {
  ESP_STATE_BOOT,
  ESP_STATE_IDLE,
  ESP_STATE_SCAN,
  ESP_STATE_CONNECTING,
  ESP_STATE_CONNECT_FAIL,
  ESP_STATE_CONNECT_SUCCESS,
  ESP_STATE_PEER_SYNCING,
  ESP_STATE_PEER_SYNC_SUCCESS,
  ESP_STATE_PEER_SYNC_FAIL,
  ESP_STATE_WIPING_DATA,
  ESP_STATE_WIPED,
  ESP_STATE_HOME_SYNCING,
  ESP_STATE_HOME_SYNC_FAIL,
  ESP_STATE_HOME_SYNC_SUCCESS
  ESP_STATE_SHUTDOWN
} AppState;

const static EventGroupHandle_t wifi_event_group;
uint32_t irq_source;
TaskHandle_t irqHandlerTask = NULL;

void irqHandler( void *pvParams ) {
  _ASSERT( (uint32_t)pvParams == 1 );
  uint32_t irq_source;

  for ( ;; ) {
    xTaskNotifyWait(0x00,           // don't clear bits on entry
                    ULONG_MAX,      // clear all bits on exit
                    &irq_source,    // receives notif value
                    portMAX_DELAY); // wait forever
    if ( irq_source & UNMASK_IRQ ) {
      irq_source &= ~MASK_IRQ;
    } else if ( (irq_source & MASK_IRQ) ) {
      // LoRa check happens here - os_querytimecriticaltasks
      continue;
    }

    if ( irq_source & DISPLAY_IRQ ) {
      display_refresh();
    }
    // sleep cycle happens in ISR too.
    // we can handle sending state over UART in a lowest-prio task
  }
}

void IRAM_ATTR doIrq( int irq ) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xTaskNotifyFromISR(irqHandlerTask, irq, eSetBits, &xHigherPriorityTaskWoken);
  if ( xHigherPriorityTaskWoken ) {
    portYIELD_FROM_ISR();
  }
}

////


static void init_nvs( void ) {
  esp_err_t err = nvs_flash_init();
  if ( err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND ) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

/*
 * since our app flow is relatively simple, should use an enum + direct-to-task notifs.
 * we make an enum full of u32 integer flags.
 * use xTaskNotify if the task only needs to know one piece of state at a time
 * or xTaskNotifyIndexed if it has to be aware of multiple different state flags.
 *
 * this is way faster and more performant / lower-RAM than using event groups or messaging etc.
 *
 * i think one top-level "broker" task controlling state mgmt 
 * but the lower-level sync type tasks have ultimate priority and give back to broker only
 * when they're ready to relinquish.
 *
 */

void app_main( void ) {


/* HARDWARE SETUP */


  // SD CARD / FILESYSTEM
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

  // SDMMC PINS
  gpio_set_pull_mode(15, GPIO_PULLUP_ONLY); // CMD
  gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);  // D0
  gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);  // D1
  gpio_set_pull_mode(12, GPIO_PULLUP_ONLY); // D2
  gpio_set_pull_mode(13, GPIO_PULLUP_ONLY); // D3 

  esp_err_t err = init_sd();
  if ( err != ESP_OK ) {
    ESP_LOGE(TAG, "Failed to initialize SD in main.");
  }

  // WIFI
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK( esp_event_loop_create_default() );
  init_wifi( wifi_event_group );


  // create the tasks

  if ( xTaskCreate( 
}

static void wifi_event_handler( void *arg, esp_event_base_t event_base, i32 event_id, void *event_data ) {

  if ( event_id == WIFI_EVENT_AP_STACONNECTED ) {
    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
    // now this is dumb.
    ht_insert( mac_ip_table, (char*)event.mac, (char*)0 );
    ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
  } 

  if ( event_id == WIFI_EVENT_AP_STADISCONNECTED ) {
    wifi_event_ap_stadisconnected_t *event = ( wifi_event_ap_stadisconnected_t*) event_data;
    ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
  }

}

static void ip_event_handler( void *arg, esp_event_base_t event_bases, i32 event_id, void *event_data ) {

  switch ( event_id ) {

    case IP_EVENT_AP_STAIPASSIGNED: {
      esp_ip4_addr_t station_ip = (ip_event_ap_staipassigned_t) event_data->ip;
      printf("station assigned ip: %s\n", ip4addr_ntoa(&station_ip) ); 
      // assign our ip in our friends data structure.  hashtable lookup
      char ip = event_data->ip.addr;
      int res = ht_update( &mac_ip_table, (char*)event_data->mac, &ip );
      // can't even error check this rn bc hash table update always returns 1
      break;
    }
    default:
      break;

  }
}

int reconnect_callback( TimerHandle_t timer ) {

  ESP_ERROR_CHECK( esp_wifi_scan_start( &scan_config, false ) );
  ESP_ERROR_CHECK( esp_wifi_scan_get_ap_records( 20, access_points ) );
   
  for ( int i=0; i < NUM_FRIEND_IDS; i++ ) {
    for ( int j=0; j < 20; j++ ) {
      // would want a way to temporarily "blacklist" friends recently
      // connected to and synced so we don't just endlessly reconnect.
      // could implement friends as a linked-list of structs, friend id
      // + metadata like recently synced flag etc
      long ssid = strtol( access_points[j].ssid )
      if ( ssid == friends_list[i].id ) {
        strncpy( ap_config.ssid, access_points[j].ssid, 33 );
        ap_config.ssid_len = strnlen( ap_config.ssid, 32 );
        strncpy( ap_config.password, friends_list[i].pass, 64 );
        ap_config.authmode = WIFI_AUTH_WPA3_PSK;
        ap_config.pairwise_cipher = WIFI_CIPHER_TYPE_AES_GMAC256;
      } else {
        ESP_LOGI(tag, "failed to find a valid password for id %s:", access_points[j].ssid );
        return 0;
      }
      esp_err_t connect_err;
      connect_err = esp_wifi_connect();
      if ( connect_err != ESP_OK ) {
        ESP_LOGE(tag, "failed to connect to ssid %s", ap_config.ssid );
        return 0;
      } else {
        xEventGroupSetBits( wifi_event_group, WIFI_EVENT_CONNECTED );
        return 1;
      }
    }
  }

}


















// https://techtutorialsx.com/2019/09/22/esp32-arduino-soft-ap-obtaining-ip-address-of-connected-stations/

void task_wifi ( void *pvParam ) { 
  while ( ;; ) {
    switch( xEventGroupGetBits( wifi_event_group ) ) {
      case WIFI_EVENT_DISCONNECTED:
        // just return because we handle this in the timer
        return;
      // need separate flags for connected to AP and someone connected to US
      case WIFI_EVENT_CONNECTED:

        wifi_sta_list_t wifi_sta_list;
        tcpip_adapter_sta_list_t tcpip_sta_list;
        memset(&wifi_sta_list, 0, sizeof(wifi_sta_list_t));
        memset(&tcpip_sta_list, 0, sizeof(tcpip_adapter_sta_list_t));

        esp_wifi_ap_get_sta_list(&wifi_sta_list);
        tcpip_adapter_get_sta_list(&wifi_sta_list, &tcpip_sta_list);
        
        for ( int i=0; i < tcpip_sta_list.num; i++ ) {
          tcpip_adapter_sta_info_t station = tcpip_sta_list[i];
          
        } 
    



    }
    if ( xEventGroupGetBits( wifi_event_group ) == WIFI_EVENT_NO_SSIDS_FOUND ) {

    }
  }
}

void wifi_init ( void ) {
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();

  mac_ip_table = new_ht();

  ESP_ERROR_CHECK( esp_wifi_init(&config) );

  ESP_ERROR_CHECK( esp_event_handler_instance_register( WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL, NULL ) );
  wifi_config_t ap_config = {
    .ap = {
      .ssid = 
      .ssid_len =
      .channel = 
      .password = 
      .max_connection =
      .authmode = 
      .pmf_cfg = {
        .required = false,  // should set to true for production
      },
    },
  };

  // @FIXME
  wifi_config_t sta_config = {
    .sta = {
      .ssid = 
      .password = 
      .scan_method = WIFI_ALL_CHANNEL_SCAN,
      .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,   // or WIFI_CONNECT_AP_BY_SECURITY
      .threshold = {
        .rssi = 60,   // min acceptable RSSI
        .authmode = WIFI_AUTH_WPA2_PSK  // acceptable auth mode
      },
      .pmf_cfg = {
        .required = false,  // set to true for production
      },
    },
  };

  // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html

  wifi_scan_config_t scan_config = { 0 };
  // for now we're listening for beacon frames, but this is a massive concern.
  // is it safer to active scan and have listeners silent?  probably.
  scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;

  ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_APSTA )
  ESP_ERROR_CHECK( esp_wifi_set_config( WIFI_IF_AP, &ap_config ) );
  ESP_ERROR_CHECK( esp_wifi_set_config( WIFI_IF_STA, &sta_config ) );

  // task, name, stack depth in words, params passed in, priority 0-25
  xTaskCreate( &task_wifi, "Wifi", 10000, wifiParam, 23 );
  reconnect_timer = xTimerCreate( "reconnectTimer", 
                                  pdMS_TO_TICKS(15000),
                                  pdTRUE,
                                  0,
                                  reconnect_callback );

  wifi_event_group = xEventGroupCreate();
  // check wifi_event_group not null

  // false - non-blocking / async
  ESP_ERROR_CHECK( esp_wifi_scan_start( &scan_config, false ) );

}

