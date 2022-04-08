#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "addr_from_stdin.h"
#include "lwip/err.h"
#include "lwip/sockets.h"

static const char *TAG = "socket_client";
static const char *test_payload = "bepp bepp";

int send_file( const char *path ) {
  char buffer[FILE_BUFFER_SIZE];
  FILE *f = fopen(path, "wb");
  fseek(f, 0L, SEEK_END);
  int len = ftell(f);
  int to_write = size;
  rewind(f);

  while ( to_write > 0 ) {
    int read = fread(buffer, sizeof(char), 1024, f);
    int wrote = send(sock, buffer, read, 0);
    to_write -= wrote;
  }
}

static void task( void *pvParams ) {
  char rx_buffer[128];
  char host_ip[] = HOST_IP_ADDR;
  int addr_family = 0;
  int ip_protocol = 0;

  while ( ;; ) {
    // just do IPV4 for now
    struct sockaddr_in destination;
    destination.sin_addr.s_addr = inet_addr(host_ip);
    destination.sin_family = AF_INET;
    destination.sin_port = htons(PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if ( sock < 0 ) {
      ESP_LOGE(TAG, "unable to create socket: errno %d", errno);
      break;
    }
    ESP_LOGI(TAG, "socket created, connecting to %s:%d", host_ip, PORT);

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
    if ( err != 0 ) {
      ESP_LOGE(TAG, "unable to connect: errno %d", errno);
      break;
    }
    ESP_LOGI(TAG, "successfully connected!");
    
    while ( 1 ) {
      int err = send(sock, payload, strlen(payload), 0);

      int len = recv(sock, rx_buffer, sizeof(rx_buffer)-1, 0);

      else {

      }
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    if ( sock != -1 ) {
      shutdown(sock, 0);
      close(sock);
    }
  }
  vTaskDelete(NULL);
}
