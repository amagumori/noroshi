#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"

static const int RX_BUFFER_SIZE = 1024;
QueueHandle_t sync_state_queue;

#define TX_PIN (GPIO_NUM_1)
#define RX_PIN (GPIO_NUM_2)

void init ( void ) {
  const uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
  };

  uart_driver_install(UART_NUM_1, RX_BUFFER_SIZE * 2, 0, 0, NULL, 0);
  uart_param_config(UART_NUM_1, &uart_config);
  uart_set_pint(UART_NUM_1, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

int send( const char *log_name, const char *data ) {
  const int len = strlen(data);
  const int tx_bytes = uart_write_bytes(UART_NUM_1, data, len);
  ESP_LOGI(log_name, "wrote %d bytes", tx_bytes);
  return tx_bytes;
}

static void tx_task( void *arg ) {
  static const char *TX_TASK_TAG = "TX_TASK";
  esp_log_level_set(TX_TASK_TAG, ESP_LOG_INFO);
  while (1) {
    send(TX_TASK_TAG, "hello world.");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}
