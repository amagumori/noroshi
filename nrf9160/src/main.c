#include <device.h>
#include <drivers/display.h>
#include <stdio.h>
#include <string.h>

#include <zephyr.h>
#include <lvgl.h>

#include "modules/ui.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(app);

// include logo image as C bitmap array
extern const lv_img_dsc_t logo;

typedef enum AppState {
  STARTUP,
  CONNECTING,
  CONNECTED_IDLE,
  CONNECTION_LOST,
  TALKING,
  LISTENING
};

void main ( void ) {
  // UI
  init_ui();
  // MODEM
  
  // MQTT
  init_client();
}
