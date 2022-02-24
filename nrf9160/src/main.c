#include <device.h>
#include <drivers/display.h>
#include <stdio.h>
#include <string.h>

#include <zephyr.h>
#include <lvgl.h>

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
  const struct device *display_dev;
  display_dev = device_get_binding(CONFIG_LVGL_DISPLAY_DEV_NAME);

  static lv_style_t style_screen;
  lv_style_init(&style_screen);
  lv_style_set_bg_color(&style_screen, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_add_style(lv_scr_act(), LV_OBJ_PART_MAIN, &style_screen);

  lv_obj_t *img = lv_img_create(lv_scr_act(), NULL);
  lv_img_set_src(img, &logo);
  lv_obj_align(img, NULL, LV_ALIGN_CENTER, 0, 0);

  lv_task_handler();
  display_blanking_off(display_dev);
}
