#include "uECC.h"


static const char *TAG = "ESP-NOW";
/* attempt at using micro-ecc and hw AES functions
 * to negotiate connections, compute friends list intersection with PSI, etc.
 *
 * https://cryptobook.nakov.com/asymmetric-key-ciphers/ecdh-key-exchange
 */

/*
 * the general idea: DISPLAY NAME is broadcasted as SSID.
 * can't broadcast public key bc then we have no way to find / id un-auth'd friends
 * get mac from AP scan
 */

#define WAKE_WAIT 5000

extern int ap_count;
extern wifi_ap_record_t ap_records[20];

static QueueHandle_t espnow_queue;
static MessageBufferHandle_t peer_to_connect;

typedef struct friend_t {
  char name[32]; // identifier that will be broadcast pre-connection
  // ideally would show our number of "mutuals" thru PSI somehow
  u8 publickey[64];
  u8 secret[64];
} friend;


typedef struct friend_packet {
  char name[32];
  u8 publickey[33];
} friend_packet_t;

typedef enum {
    ESPNOW_SEND,
    ESPNOW_RECV
} espnow_event_id_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} espnow_event_send_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} espnow_event_recv_t;

typedef union {
    espnow_event_send_t send;
    espnow_event_recv_t receive;
} espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct {
    espnow_event_id_t id;
    espnow_event_info_t info;
} espnow_event_t;

static void espnow_recv_cb( const u8 *mac, const u8 *data, int len ) {
  espnow_event_t evt;
  espnow_event_recv_t *recv = &evt.info.receive;

  if ( mac == NULL || data == NULL || len <= 0 ) {
    ESP_LOGE(TAG, "receive cb argument error...");
    return;
  }
e
  evt.id = ESPNOW_RECV;
  memcpy(receive->mac_addr, mac, ESP_NOW_ETH_ALEN);
  receive->data = malloc(len);
  if ( receive->data == NULL ) {
    ESP_LOGE(TAG, "error mallocing receive data.");
    return;
  } 
  memcpy(receive->data, data, len);
  receive->data_len = len;

  if ( xQueueSend(espnow_queue, &evt, ESPNOW_MAXDELAY ) != pdTRUE ) {
    ESP_LOGW(TAG, "failed to post recv msg to recv queue.");
    free(receive->data);
  }
}

void init() {
  size_t peer_ap_size = sizeof(wifi_ap_record_t);
  peer_to_connect = xMessageBufferCreate( peer_ap_size);
  ESP_ERROR_CHECK( esp_now_set_wake_window( WAKE_WAIT );

}

void add_peers ( void *pvParams ) {
  static u32 notif;
  static esp_now_peer_num_t peer_count;
  // only wake up when we fire the notification from the wifi scan.
  notif = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  struct esp_now_peer_info_t peer_info = {0}; 
  struct wifi_ap_record_t peer_ap;
  size_t peer_size = sizeof(wifi_ap_record_t);

  for ( int i=0; i < ap_count; i++ ) {

    // @FIXME 
    // how to determine if a AP really is a peer device with only AP data?
    // could put a magic number in the SSID?

    if ( strstr("bepp", ap_records[i].ssid ) != NULL ) {
      
      strncpy( peer_info.peer_addr, ap_records[i].bssid, 5 );
      peer_info.channel = peer_ap.primary;
      peer_info.ifidx = WIFI_IF_AP; // i guess?
      peer_info.encrypt = false;
      
      esp_now_add_peer( &peer_info );
    }
  }
  ESP_ERROR_CHECK( esp_now_get_peer_num( peer_count ) );
  ESP_LOGI( TAG, "added %d peers.", peer_count.total_num ); 

}

// https://cryptobook.nakov.com/asymmetric-key-ciphers/ecdh-key-exchange
// https://github.com/pcbreflux/espressif/blob/master/esp32/app/ESP32_micro-ecc/main/ecc_task.c

void key_exchange( void *pvParams ) {

  /* This wakes up when receiving a message with the BSSID to key-xchange with. */
 
  friend_packet_t packet;

  uECC_Curve curve;
  esp_aes_context ctx;

  curve = uECC_secp256r1();

  u32 loop = 0;
  u8 private_key[32];
  u8 public_key[64];
  u8 public_key_comp[64];   // computed publickey
  int private_key_size;
  int public_key_size;
  u8 peer_public_key[64];
  u8 secret[32];

  // generate my keypair
  int ret = uECC_make_key( public_key, private_key, curve );  
  if ( ret != 1 ) {
    ESP_LOGE(TAG, "Error generating local ECC keypair.");
  }
  private_key_size = uECC_curve_private_key_size(curve);
  public_key_size =  uECC_curve_public_key_size(curve);

  ret = uECC_compute_public_key( private_key, public_key_comp, curve );

  xQueueReceive( espnow_queue, &packet, portMAX_DELAY );

  uECC_decompress( packet.publickey, peer_public_key, curve );
  ret = uECC_valid_public_key( peer_public_key, curve );
  if ( ret != 1 ) {
    ESP_LOGE(TAG, "received an invalid public key.");
  }
  
  ret = uECC_shared_secret( peer_public_key, private_key, secret, curve );

  struct friend_t friend = {0};

  memcpy( friend.name, packet.name, 32 );
  memcpy( friend.publickey, peer_public_key, 64 );
  memcpy( friend.secret, secret, 64 ); 

  // @TODO 
  // put this in "friend storage"

  // then hash the shared secret with AES256 and do stuff...
  
}
