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

struct app_msg {
	union {
		struct modem_event modem;
		struct ui_event ui;
		struct radio_event radio;
		struct motorola_event motorola;
		struct server_event server;
		struct app_event app;
	} module;
};

#define APP_QUEUE_ENTRY_COUNT		10
#define APP_QUEUE_BYTE_ALIGNMENT	4

K_MSGQ_DEFINE( msgq_app, sizeof( struct app_msg ), APP_QUEUE_ENTRY_COUNT, APP_QUEUE_BYTE_ALIGNMENT );

static struct module_data self = {
	.name = "app",
	.msg_q = &msgq_app,
	.supports_shutdown = true,
};

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_RUNNING:
		return "STATE_RUNNING";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

/* enumerate substates a bit later
 *
static char *sub_state2str(enum sub_state_type new_state)
{
	switch (new_state) {
	case SUB_STATE_ACTIVE_MODE:
		return "SUB_STATE_ACTIVE_MODE";
	case SUB_STATE_PASSIVE_MODE:
		return "SUB_STATE_PASSIVE_MODE";
	default:
		return "Unknown";
	}
}
*/

static void state_set(enum state_type new_state)
{
	if (new_state == state) {
		LOG_DBG("State: %s", state2str(state));
		return;
	}

	LOG_DBG("State transition %s --> %s",
		state2str(state),
		state2str(new_state));

	state = new_state;
}

static void sub_state_set(enum sub_state_type new_state)
{
	if (new_state == sub_state) {
		LOG_DBG("Sub state: %s", sub_state2str(sub_state));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s",
		sub_state2str(sub_state),
		sub_state2str(new_state));

	sub_state = new_state;
}


static bool event_handler(const struct event_header *eh)
{
	struct app_msg_data msg = {0};
	bool enqueue_msg = false;

	if (is_cloud_module_event(eh)) {
		struct cloud_module_event *evt = cast_cloud_module_event(eh);

		msg.module.cloud = *evt;
		enqueue_msg = true;
	}

	if (is_app_module_event(eh)) {
		struct app_module_event *evt = cast_app_module_event(eh);

		msg.module.app = *evt;
		enqueue_msg = true;
	}

	if (is_data_module_event(eh)) {
		struct data_module_event *evt = cast_data_module_event(eh);

		msg.module.data = *evt;
		enqueue_msg = true;
	}

	if (is_sensor_module_event(eh)) {
		struct sensor_module_event *evt = cast_sensor_module_event(eh);

		msg.module.sensor = *evt;
		enqueue_msg = true;
	}

	if (is_util_module_event(eh)) {
		struct util_module_event *evt = cast_util_module_event(eh);

		msg.module.util = *evt;
		enqueue_msg = true;
	}

	if (is_modem_module_event(eh)) {
		struct modem_module_event *evt = cast_modem_module_event(eh);

		msg.module.modem = *evt;
		enqueue_msg = true;
	}

	if (is_ui_module_event(eh)) {
		struct ui_module_event *evt = cast_ui_module_event(eh);

		msg.module.ui = *evt;
		enqueue_msg = true;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(app, APP_EVT_ERROR, err);
		}
	}

	return false;
}

static void on_all_states( struct app_msg *msg ) {
  //if ( IS_EVENT( msg, 
}


void main ( void ) {

  int err;
  struct app_msg msg;

  if ( event_manager_init() ) {
    LOG_ERR( "APP: couldn't initialize event manager, rebooting.");
    k_sleep( K_SECONDS(4) );
    sys_reboot(SYS_REBOOT_COLD);
  } else {
    module_set_state( MODULE_STATE_READY );
    SEND_EVENT( app, APP_EVENT_START );
  }

  self.thread_id = k_current_get();
  err = module_start(&self);
  if ( err ) {
    LOG_ERR("failed starting module: error %d.", err );
    SEND_ERROR( app, APP_EVENT_ERROR, err );
  }

  // watchdog?

  while ( true ) {
    module_get_next_msg(&self, &msg);

    switch ( state ) {

    }
  }
}




}

EVENT_LISTENER( MODULE, event_handler );
EVENT_SUBSCRIBE( MODULE, modem_event );
EVENT_SUBSCRIBE( MODULE, motorola_event );  // i badly need a better naming convention.
EVENT_SUBSCRIBE( MODULE, radio_event );
// probably implement event messages from hw too later?
EVENT_SUBSCRIBE( MODULE, server_event );
EVENT_SUBSCRIBE( MODULE, input_event );
