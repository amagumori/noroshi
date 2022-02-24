#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#define TAG "wifi"

int init_wifi ( EventGroupHandle_t wifi_event_group );
void start_wifi( void );
