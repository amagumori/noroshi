struct fileinfo_tuple {
  const char path[PATH_MAX];
  struct time_t modified;
};

// walk dir tree and push tuples into the espnow q.
// on the recipient side, add them to hashtable

static send_cb ( void ) {

}

static recv_cb ( void ) {

}

static esp_err_t init_espnow( void ) {
  send_param_t *send_param;

  ESP_ERROR_CHECK( esp_now_init() );
  ESP_ERROR_CHECK( esp_now_register_send_cb(send_cb) );
  ESP_ERROR_CHECK( esp_now_register_recv_cb(recv_cb) );

  esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
  if ( peer == NULL ) {
    ESP_LOGE(TAG, "couldn't malloc peer info.");
    vSemaphoreDelete(espnow_queue);
    esp_now_deinit();
    return ESP_FAIL;
  }
  memset(peer, 0, sizeof(esp_now_peer_info_t));
  peer->channel = CONFIG_ESPNOW_CHANNEL;
  peer->ifidx = ESPNOW_WIFI_IF;
  peer->encrypt = false;
  memcpy(peer->peer_addr, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
  ESP_ERROR_CHECK( esp_now_add_peer(peer) );
  free(peer);

  /* Initialize sending parameters. */
  send_param = malloc(sizeof(send_param_t));
  memset(send_param, 0, sizeof(send_param_t));
  if (send_param == NULL) {
      ESP_LOGE(TAG, "Malloc send parameter fail");
      vSemaphoreDelete(espnow_queue);
      esp_now_deinit();
      return ESP_FAIL;
  }
  send_param->unicast = false;
  send_param->broadcast = true;
  send_param->state = 0;
  send_param->magic = esp_random();
  send_param->count = CONFIG_ESPNOW_SEND_COUNT;
  send_param->delay = CONFIG_ESPNOW_SEND_DELAY;
  send_param->len = CONFIG_ESPNOW_SEND_LEN;
  send_param->buffer = malloc(CONFIG_ESPNOW_SEND_LEN);
  if (send_param->buffer == NULL) {
      ESP_LOGE(TAG, "Malloc send buffer fail");
      free(send_param);
      vSemaphoreDelete(espnow_queue);
      esp_now_deinit();
      return ESP_FAIL;
  }
  memcpy(send_param->dest_mac, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
  example_espnow_data_prepare(send_param);

  xTaskCreate(example_espnow_task, "example_espnow_task", 2048, send_param, 4, NULL);

  return ESP_OK;

}
