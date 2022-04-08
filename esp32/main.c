#include "wifi.h"

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
void IRAM_ATTR DisplayIRQ() { doIrq(DISPLAY_IRQ) }


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
 * we want to use xEventGroupSetBits instead of using IRQs directly...?
 */

void app_main( void ) {

  // WIFI
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK( esp_event_loop_create_default() );
  init_wifi( wifi_event_group );

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
}
