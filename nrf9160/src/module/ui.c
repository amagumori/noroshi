#include <lvgl.h>
#include <event_manager.h>
#include <event_manager_profiler.h>

#include <device.h>
#include <drivers/display.h>

#include "lcd/oled_ssd1351.h"
#include "users.h"

#define LVGL_DISPLAY_DEV_NAME display

#define MAX_USERS 24
#define MAX_USERNAME_LENGTH 36 

const char* icon_path = "./icon.bin";

static const device *display_dev;
static const lv_theme_t *theme;

// LV_FONT_DECLARE( myfont );
static const lv_font_t *font;
static const char currently_talking[MAX_USERNAME_LENGTH];
static const u32 user_id_list[MAX_USERS];

static const char username_list[MAX_USERNAME_LENGTH][MAX_USERS];

static enum screen_id {
  BOOT = 0,
  CONNECTING_LTE,
  CONNECTING_SERVER,
  AUTHENTICATING,
  AUTH_FAILED,
  CONNECTED_IDLE,
  LISTENING,
  TRANSMITTING,
  SHUTDOWN,
  SCREEN_COUNT
};

static const lv_screen_t screens[SCREEN_COUNT];
// top level container
static const lv_obj_t *container;

static const lv_obj_t *list_view;
static const lv_style_t *style_username;

static const lv_obj_t *listen_view;
static const lv_obj_t *currently_talking_label;

struct ui_msg {
  union {
    struct hw_event     hw;
    struct modem_event  modem;
    struct radio_event  radio;
    struct server_event server;
    struct i2s_event    i2s
  } module;
}

static enum state_type {
  STATE_BOOT,
  STATE_CONNECTING_LTE,
  STATE_CONNECTING_SERVER,
  STATE_DISCONNECTED,
  STATE_INCOMING_MSG,
  STATE_LISTENING_IDLE, 
  STATE_LISTENING_ACTIVE,  
  STATE_TRANSMITTING,
  STATE_BEGIN_SHUTDOWN,
} state;

static struct module_data self = {
  .name = "ui",
  .msg_q = &ui_msgq,
  .supports_shutdown = true,
};

#define UI_QUEUE_ENTRY_COUNT 10 
#define UI_QUEUE_ALIGNMENT   4

K_MSGQ_DEFINE(ui_msg_queue, sizeof(struct ui_msg), UI_QUEUE_ENTRY_COUNT, UI_QUEUE_ALIGNMENT);

static void set_state( enum state_type new_state ) {
  if ( new_state != state ) {
    state = new_state;
  } else {
    LOG_DBG("state is still %s.", state);
  }
}

void message_handler( ui_msg *msg ) {
  if ( IS_EVENT( msg, modem, MODEM_EVT_LTE_CONNECTING) ) {
    set_state( STATE_CONNECTING_LTE );
    lv_scr_load( screens[CONNECTING_LTE] );
  }
  if ( IS_EVENT( msg, modem, MODEM_EVT_LTE_CONNECTED ) ) {
    set_state( STATE_CONNECTED );
  }
  if ( IS_EVENT( msg, server, SERVER_EVENT_BEGIN_AUTH) ) {
    set_state( STATE_CONNECTING_SERVER );
    lv_scr_load( screens[CONNECTING_SERVER] );
  }
  if ( IS_EVENT( msg, server, SERVER_EVENT_AUTH_DENIED) ) {
    set_state( STATE_DISCONNECTED );
    lv_scr_load( screens[AUTH_FAILED] );
  }
  if ( IS_EVENT( msg, server, SERVER_EVENT_CONNECTED) ) {
    set_state( STATE_CONNECTED );
    lv_scr_load( screens[CONNECTED_IDLE] );
  }
  if ( IS_EVENT( msg, server, SERVER_EVENT_BEGIN_AUTH) ) {
    set_state( STATE_CONNECTING_LTE );
    lv_scr_load( screens[AUTHENTICATING] );
  }
  if ( IS_EVENT( msg, radio, RADIO_EVENT_INCOMING_MSG_START ) ) {
    set_state( STATE_LISTENING_ACTIVE );
    lv_scr_load( screens[LISTENING] );
  }

  if ( IS_EVENT( msg, hw, HW_EVENT_PTT_DOWN ) ) {
    set_state( STATE_TRANSMITTING );
    view_transmitting();
  }
}

static bool event_handler( const struct event_header *eh ) {
  if ( is_modem_module_event( eh ) ) {
    struct modem_event *e = cast_modem_event(eh);
    struct ui_msg msg = {
      .module.modem = *event
    };
    message_handler(&msg);
  }

  if ( is_radio_module_event( eh ) ) {
    struct radio_event *e = cast_radio_event(eh);
    struct ui_msg msg = {
      .module.radio = *event
    };
    message_handler(&msg);
  }

  if ( is_server_module_event( eh ) ) {
    struct server_event *e = cast_server_event(eh);
    struct ui_msg msg = {
      .module.server = *event
    };
    message_handler(&msg);
  }

  if ( is_hw_module_event( eh ) ) {
    struct hw_event *e = cast_hw_event(eh);
    struct ui_msg msg = {
      .module.hw = *event
    };
    message_handler(&msg);
  }

  return false;
}

void init_ui( void ) {

  display_dev = device_get_binding( LVGL_DISPLAY_DEV_NAME );
  theme = lv_theme_get_act();

  ssd1351_128x128_spi_init(16, 5, 17);
  ssd1351_setMode( LCD_MODE_NORMAL );

  for ( int i=0; i < SCREEN_COUNT; i++ ) {
    screens[i] = lv_obj_create(NULL, NULL);
  }
  lv_scr_load( screens[0] );

  create_listening_screen();

  container = lv_obj_create( screens[0] );

  lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_style_all(container);
  lv_obj_set_size(container, lv_pct(100), lv_pct(100));
  lv_obj_set_scroll_snap_y(container, LV_SCROLL_SNAP_CENTER);
 
  TEST_boot_label = lv_label_create( container, NULL );
  lv_obj_set_pos( TEST_boot_label, 50, 50);
  lv_label_set_text(TEST_boot_label, "booting!");
  
}

void cleanup_ui ( void ) {
  
}

void create_listening_screen( void ) {
  lv_screen_t *screen_listen = screens[ LISTENING ];
  lv_obj_t *listen_container = lv_obj_create(screen_listen);
  lv_obj_remove_style_all(listen_container);
  lv_obj_set_height(listen_container, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(listen_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(listen_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *icon;
  LV_IMG_DECLARE(icon);
  icon = lv_obj_create(container);
  lv_img_set_src(icon, icon_path);

  currently_talking_label = lv_obj_create(listen_container);
  lv_obj_remove_style_all(currently_talking_label);
  lv_label_set_text(label, "nobody is talking right now.");
  
}

void create_connecting_lte( void ) {

}

void create_connecting_server( void ) {

}

void create_connected_idle( void ) {

}

void create_transmitting( void ) {

}

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE_EARLY(MODULE, modem_event);
EVENT_SUBSCRIBE_EARLY(MODULE, radio_event);
EVENT_SUBSCRIBE_EARLY(MODULE, hw_event);
EVENT_SUBSCRIBE_EARLY(MODULE, server_event);

// this isn't strictly necessary but for an instant response when powering on is nice.
SYS_INIT(init_ui, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
