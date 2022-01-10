#include <zephyr.h>
#include <event_manager.h>
#include <settings/settings.h>
#include <date_time.h>
#include <modem/modem_info.h>
#include "../codec/codec.h"

#define AUDIO_BUFFER_SIZE 2048  // man i dunno
#define MODEM_BUFFER_SIZE 2048  // man i dunno
#define RADIO_MAX_MESSAGES 10   // man i dunno 
#define MAX_TX_SAMPLES 21000    // at 700 samples / s this is 30 seconds.
                                // "enough time for anyone"

#include "modules_common.h"
// #include "users.h"
#include "../events/app_event.h"
#include "../events/radio_event.h"
#include "../events/modem_event.h"
#include "../events/ui_event.h"

#include "../hw/mic.c"

/* 
 *
 * DATA LAYER - PACKET-LEVEL EVENTS
 *
 */

struct data_msg {
  union {
    struct modem_event modem;
    struct app_event app;
    struct radio_event radio;
  } module;
};

// types of data sent out 
enum data_type {
  AUDIO_UNENC,
  AUDIO_ENC,
  NETWORK,
  // a lot more.
};

struct data_buffer {
  char *buf;
  size_t len;
};

struct radio_event_type {
  RADIO_SEND_AUDIO,
  RADIO_RECV_AUDIO,
  RADIO_ERROR,
  //etc
};

struct radio_event {
  struct event_header header;
  enum radio_event_type type;

  union {
    struct data_buffer data;
    u32 id;
    int err;
  } data;
};

EVENT_TYPE_DECLARE(radio_event);

/*
 *
 * MODEM LAYER - LTE-level events
 *
 *
 */

struct modem_param_info modem_info;

static i16 rsrp_value_latest;
// const k_tid_t module_thread; 
// we'll wait to break everything out into threads

enum modem_event_type {
	MODEM_EVT_INITIALIZED,
	MODEM_EVT_LTE_CONNECTED,
	MODEM_EVT_LTE_DISCONNECTED,
	MODEM_EVT_LTE_CONNECTING,
	MODEM_EVT_LTE_CELL_UPDATE,
	MODEM_EVT_LTE_PSM_UPDATE,
	MODEM_EVT_LTE_EDRX_UPDATE,
	MODEM_EVT_MODEM_STATIC_DATA_READY,
	MODEM_EVT_MODEM_DYNAMIC_DATA_READY,
	MODEM_EVT_MODEM_STATIC_DATA_NOT_READY,
	MODEM_EVT_MODEM_DYNAMIC_DATA_NOT_READY,
	MODEM_EVT_NEIGHBOR_CELLS_DATA_READY,
	MODEM_EVT_NEIGHBOR_CELLS_DATA_NOT_READY,
	MODEM_EVT_BATTERY_DATA_NOT_READY,
	MODEM_EVT_BATTERY_DATA_READY,
	MODEM_EVT_SHUTDOWN_READY,
	MODEM_EVT_ERROR,
	/** The carrier library has initialized the modem library and it is
	 *  now ready to be used. When the carrier library is enabled, this
	 *  event must be received before the modem module can proceed to initialize
	 *  other dependencies and subsequently send MODEM_EVT_INITIALIZED.
	 */
	MODEM_EVT_CARRIER_INITIALIZED,
	/** Due to modem limitations for active TLS connections, the carrier
	 *  library requires all other TLS connections in the system to
	 *  be terminated while FOTA update is ongoing.
	 */
	MODEM_EVT_CARRIER_FOTA_PENDING,
	/** FOTA update has been stopped and the application can set up TLS
	 *  connections again.
	 */
	MODEM_EVT_CARRIER_FOTA_STOPPED,
	/** The carrier library requests that the device reboots to apply
	 *  downloaded firmware image(s) or for other reasons.
	 */
	MODEM_EVT_CARRIER_REBOOT_REQUEST,

};

struct modem_psm {
  int tau;
  int active_time;
}; 

struct modem_cell {
  // U-TRAN id
  u32 cell_id;
  // Tracking Area Code
  u32 tac;
};

struct modem_edrx {
  // edrx interval in s
  float edrx;
  // paging time window
  float ptw;
};

struct modem_battery {
  u16 voltage;
  i64 timestamp;
};

struct modem_neighbor_cells {
  struct lte_lc_cells_info cell_data;
  struct lte_lc_ncell neighbor_cells[17];
  i64 timestamp;
};


struct modem_event {
  struct event_header header;
  enum modem_event_type type;
  union {
    struct modem_battery_data battery;
    struct modem_cell cell;
    struct modem_psm psm;
    struct modem_edrx edrx;
    struct modem_neighbor_cells neighbor_cells;
    u32 id;
    int err;
  } data;
};

EVENT_TYPE_DECLARE(modem_event);

// RING BUFFERS

struct radio_transmission {
  u32 user_id;
  i16 buffer[MAX_TX_SAMPLES];
};

// static later
struct radio_transmission incoming_messages[RADIO_MAX_MESSAGES];
int incoming_messages_head;
struct radio_transmission my_transmission;

static struct modem_data modem_buffer[MODEM_BUFFER_SIZE];
static int modem_buffer_head;

#define DATA_QUEUE_ENTRY_COUNT 10
#define DATA_QUEUE_ALIGNMENT 4
K_MSGQ_DEFINE(msgq_data, sizeof(struct data_msg), DATA_QUEUE_ENTRY_COUNT, DATA_QUEUE_ALIGNMENT);

/*
 * initialize the audio buffers
 * call PDM initialization code for the mic and codec in mic.h
 */

int init_radio( void ) {
  // @TODO stubb
  u32 my_id = get_my_id();
  my_transmission = { .id = my_id };
  my_transmission.buffer = { 0 };

  for ( u32 i=0; i < RADIO_MAX_MESSAGES; i++ ) {
    incoming_messages[i] = { 0, { 0 } };
  }

  init_pdm(my_transmission.buffer);
  return 0;
}

static bool event_handler(const struct event_header *header) {
  struct data_msg msg = {0};
  bool enqueue_msg = false;

  if ( is_modem_event(header) ) {
    struct modem_event *e = cast_modem_event(eh);
    msg.module.modem = *e;
    enqueue_msg = true;
  }
  if ( is_radio_event(header) ) {
    struct radio_event *e = cast_radio_event(eh);
    msg.module.radio = *e;
    enqueue_msg = true;
  }
  if ( is_app_event(header) ) {
    struct app_event *e = cast_app_event(eh);
    msg.module.app = *e;
    enqueue_msg = true;
  }
  if ( enqueue_msg ) {
    int err = k_msgq_put(msgq_data, msg, K_NO_WAIT);
    if ( err ) {
      LOG_WARNING("message couldn't be queued up.  err code: %d", err);
      // purge the queue so the caller can process new events and isn't blocked.
      k_msgq_purge(msgq_data);
    }
  }
}

// doing this the dumbest and least generic way first.
static void send_data( struct radio_event_type type, struct data_buffer *data ) {
  struct radio_event *event = new_radio_event();
  event->type = type;

  event->data.buf = data->buf;
  event->data.len = data->len;

  radio_list_add(data->buf, data->len, type);
  EVENT_SUBMIT(event);

  data->buf = NULL;
  data->len = 0;
}

static int init_modem_data( void ) {
  int err;

  err = modem_info_init();
  if ( err ) {
    LOG_ERROR("error: modem_info_init: %d", err);
    return err;
  }

  err = modem_info_params_init(&modem_info);
  if ( err ) {
    LOG_ERROR("error modem_info_params_init: %d", err);
    return err;
  }

  err = modem_info_rsrp_register(rsrp_handler);
  if ( err ) {
    LOG_ERROR("error modem_info_rsrp_register: %d", err);
    return err;
  }

  return 0;
}

static void lte_event_handler(const struct lte_lc_evt *const event) {
  switch( event->type ) {
    case LTE_LC_EVT_NW_REG_STATUS:
      if ( event->nw_reg_status == LTE_LC_NW_REG_UICC_FAIL ) {
        LOG_ERROR("no SIM card recognized!");
        SEND_ERROR(modem, MODEM_EVENT_ERROR, -ENOTSUP);
        break;
      }

      if ( (event->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
           (event->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING) ) {
        SEND_EVENT(modem, MODEM_EVENT_LTE_DISCONNECTED);
        break;
      }

      LOG_DEBUG("network registration status: %s",
          event->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
          "connected to home network" : "connected - roaming");

      if ( !IS_ENABLED(CONFIG_LWM2M_CARRIER) ) {
        SEND_EVENT(modem, MODEM_EVENT_LTE_CONNECTED);
      }

      break;

    case LTE_LC_EVT_PSM_UPDATE:
      LOG_DEBUG("PSM param update: TAU: %d, active time: %d",
          event->psm_cfg.tau, event->psm_cfg.active_time);
      send_psm_update(event->psm_cfg.tau, event->psm_cfg.active_time);
      break;

    case LTE_LC_EVT_EDRX_UPDATE: {

      char log_buf[60];
      ssize_t len;

      len = snprintf(log_buf, sizeof(log_buf),
               "eDRX parameter update: eDRX: %.2f, PTW: %.2f",
               evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
      if (len > 0) {
        LOG_DEBUG("%s", log_strdup(log_buf));
      }

      send_edrx_update(evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
      break;

	  }
    case LTE_LC_EVT_RRC_UPDATE:
      LOG_DEBUG("RRC mode: %s",
        evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
        "Connected" : "Idle");
      break;
    case LTE_LC_EVT_CELL_UPDATE:
      LOG_DEBUG("LTE cell changed: Cell ID: %d, Tracking area: %d",
        evt->cell.id, evt->cell.tac);
      send_cell_update(evt->cell.id, evt->cell.tac);
      break;
    case LTE_LC_EVT_NEIGHBOR_CELL_MEAS:
      if (evt->cells_info.current_cell.id != LTE_LC_CELL_EUTRAN_ID_INVALID) {
        LOG_DEBUG("Neighbor cell measurements received");
        send_neighbor_cell_update((struct lte_lc_cells_info *)&evt->cells_info);
      } else {
        LOG_DEBUG("Neighbor cell measurement was not successful");
      }

      break;

    default:
      break;
  }
}

static void submit_cell_update( u32 cell_id, u32 tac )  {
  struct modem_event *event = new_modem_event();
  event->type = MODEM_EVENT_LTE_PSM_UPDATE;
  event->data.cell.cell_id = cell_id;
  event->data.cell.tac = tac;

  EVENT_SUBMIT(event);
}

static void submit_psm_update( int tau, int active_time ) {
  struct modem_event *event = new_modem_event();
  event->type = MODEM_EVENT_LTE_PSM_UPDATE;
  event->data.psm.tau = tau;
  event->data.psm.active_time = active_time;

  EVENT_SUBMIT(event);
}

static void submit_edrx_update( float edrx, float ptw ) {
  struct modem_event *event = new_modem_event();
  event->type = MODEM_EVENT_LTE_EDRX_UPDATE;
  event->data.edrx.edrx = edrx;
  event->data.edrx.ptw = ptw;

  EVENT_SUBMIT(event);
}

static int get_battery_data( void ) {
  int err;

  err = modem_info_params_get(&modem_info);
  if ( err ) {
    LOG_ERROR("modem_info_params_get: %d", err);
    return err;
  }

  struct modem_event *event = new_modem_event();
  event->data.battery.voltage = modem_info.device.battery.value;
  event->data.battery.timestamp = k_uptime_get();
  event->type = MODEM_EVENT_BATTERY_DATA_READY;
  
  EVENT_SUBMIT(event);

  return 0;
}

static int measure_neighboring_cells( void ) {
  int err;

  err = lte_lc_neighbor_cell_measurement(LTE_LC_NEIGHBOR_SEARCH_TYPE_DEFAULT);
  if ( err ) {
    LOG_ERROR("failed to start neighbor cell measurements: %d", err);
    return err;
  }

  return 0;
}

static int lte_connect( void ) {
  int err;

  err = lte_lc_connect_async(lte_event_handler);
  if ( err ) {
    LOG_ERROR("lte_lc_connect_async error: %d", err);
    return err;
  }

  SEND_EVENT(modem, MODEM_EVT_LTE_CONNECTING);
  return 0;
}

static void rsrp_handler( char rsrp_val ) {
  if ( rsrp_val > 97 ) {
    return;
  }

  rsrp_value_latest = rsrp_to_db(rsrp_val);
  LOG_DEBUG("new RSRP value: %d", rsrp_value_latest);
}

static inline int rsrp_to_db(int input) {
  if ( IS_ENABLED(CONFIG_MODEM_CONVERT_RSRP_AND_RSPQ_TO_DB) ) {
    return input - 140;
  }
  return input;
}

static inline int rsrq_to_db(int input) {
  if ( IS_ENABLED(CONFIG_MODEM_CONVERT_RSRP_AND_RSPQ_TO_DB) ) {
    return round(input * 0.5 - 19.5);
  }
  return input;
}

// wholesale copy and pasted.  this is just busywork bookkeeping code anyhow

#ifdef CONFIG_LWM2M_CARRIER
static void print_carrier_error(const lwm2m_carrier_event_t *evt)
{
	const lwm2m_carrier_event_error_t *err = (lwm2m_carrier_event_error_t *)evt->data;
	static const char *const strerr[] = {
		[LWM2M_CARRIER_ERROR_NO_ERROR] =
			"No error",
		[LWM2M_CARRIER_ERROR_BOOTSTRAP] =
			"Bootstrap error",
		[LWM2M_CARRIER_ERROR_CONNECT_FAIL] =
			"Failed to connect to the LTE network",
		[LWM2M_CARRIER_ERROR_DISCONNECT_FAIL] =
			"Failed to disconnect from the LTE network",
		[LWM2M_CARRIER_ERROR_FOTA_PKG] =
			"Package refused from modem",
		[LWM2M_CARRIER_ERROR_FOTA_PROTO] =
			"Protocol error",
		[LWM2M_CARRIER_ERROR_FOTA_CONN] =
			"Connection to remote server failed",
		[LWM2M_CARRIER_ERROR_FOTA_CONN_LOST] =
			"Connection to remote server lost",
		[LWM2M_CARRIER_ERROR_FOTA_FAIL] =
			"Modem firmware update failed",
		[LWM2M_CARRIER_ERROR_CONFIGURATION] =
			"Illegal object configuration detected",
	};

	__ASSERT(PART_OF_ARRAY(strerr, &strerr[err->code]), "Unhandled carrier library error");

	LOG_ERR("%s, reason %d\n", strerr[err->code], err->value);
}

static void print_carrier_deferred_reason(const lwm2m_carrier_event_t *evt)
{
	const lwm2m_carrier_event_deferred_t *def = (lwm2m_carrier_event_deferred_t *)evt->data;
	static const char *const strdef[] = {
		[LWM2M_CARRIER_DEFERRED_NO_REASON] =
			"No reason given",
		[LWM2M_CARRIER_DEFERRED_PDN_ACTIVATE] =
			"Failed to activate PDN",
		[LWM2M_CARRIER_DEFERRED_BOOTSTRAP_NO_ROUTE] =
			"No route to bootstrap server",
		[LWM2M_CARRIER_DEFERRED_BOOTSTRAP_CONNECT] =
			"Failed to connect to bootstrap server",
		[LWM2M_CARRIER_DEFERRED_BOOTSTRAP_SEQUENCE] =
			"Bootstrap sequence not completed",
		[LWM2M_CARRIER_DEFERRED_SERVER_NO_ROUTE] =
			"No route to server",
		[LWM2M_CARRIER_DEFERRED_SERVER_CONNECT] =
			"Failed to connect to server",
		[LWM2M_CARRIER_DEFERRED_SERVER_REGISTRATION] =
			"Server registration sequence not completed",
		[LWM2M_CARRIER_DEFERRED_SERVICE_UNAVAILABLE] =
			"Server in maintenance mode",
	};

	__ASSERT(PART_OF_ARRAY(strdef, &strdef[def->reason]),
		"Unhandled deferred carrier library error");

	LOG_ERR("Reason: %s, timeout: %d seconds\n", strdef[def->reason], def->timeout);
}

int lwm2m_carrier_event_handler(const lwm2m_carrier_event_t *evt)
{
	int err = 0;

	switch (evt->type) {
	case LWM2M_CARRIER_EVENT_MODEM_INIT:
		LOG_INF("LWM2M_CARRIER_EVENT_MODEM_INIT");
		SEND_EVENT(modem, MODEM_EVT_CARRIER_INITIALIZED);
		break;
	case LWM2M_CARRIER_EVENT_CONNECTING:
		LOG_INF("LWM2M_CARRIER_EVENT_CONNECTING");
		break;
	case LWM2M_CARRIER_EVENT_CONNECTED:
		LOG_INF("LWM2M_CARRIER_EVENT_CONNECTED");
		break;
	case LWM2M_CARRIER_EVENT_DISCONNECTING:
		LOG_INF("LWM2M_CARRIER_EVENT_DISCONNECTING");
		break;
	case LWM2M_CARRIER_EVENT_DISCONNECTED:
		LOG_INF("LWM2M_CARRIER_EVENT_DISCONNECTED");
		break;
	case LWM2M_CARRIER_EVENT_BOOTSTRAPPED:
		LOG_INF("LWM2M_CARRIER_EVENT_BOOTSTRAPPED");
		break;
	case LWM2M_CARRIER_EVENT_LTE_READY: {
		LOG_INF("LWM2M_CARRIER_EVENT_LTE_READY");
		SEND_EVENT(modem, MODEM_EVT_LTE_CONNECTED);
		break;
	}
	case LWM2M_CARRIER_EVENT_REGISTERED:
		LOG_INF("LWM2M_CARRIER_EVENT_REGISTERED");
		break;
	case LWM2M_CARRIER_EVENT_DEFERRED:
		LOG_INF("LWM2M_CARRIER_EVENT_DEFERRED");
		print_carrier_deferred_reason(evt);
		break;
	case LWM2M_CARRIER_EVENT_FOTA_START: {
		LOG_INF("LWM2M_CARRIER_EVENT_FOTA_START");
		SEND_EVENT(modem, MODEM_EVT_CARRIER_FOTA_PENDING);
		break;
	}
	case LWM2M_CARRIER_EVENT_REBOOT: {
		LOG_INF("LWM2M_CARRIER_EVENT_REBOOT");
		SEND_EVENT(modem, MODEM_EVT_CARRIER_REBOOT_REQUEST);

		/* 1 is returned here to indicate to the carrier library that
		 * the application will handle rebooting of the system to
		 * ensure it happens gracefully. The alternative is to
		 * return 0 and let the library reboot at its convenience.
		 */
		return 1;
	}
	case LWM2M_CARRIER_EVENT_ERROR: {
		const lwm2m_carrier_event_error_t *err = (lwm2m_carrier_event_error_t *)evt->data;

		LOG_ERR("LWM2M_CARRIER_EVENT_ERROR");
		print_carrier_error(evt);

		if (err->code == LWM2M_CARRIER_ERROR_FOTA_FAIL) {
			SEND_EVENT(modem, MODEM_EVT_CARRIER_FOTA_STOPPED);
		}
		break;
	}
	case LWM2M_CARRIER_EVENT_CERTS_INIT:
		err = carrier_certs_provision((ca_cert_tags_t *)evt->data);
		break;
	}

	return err;
}
#endif /* CONFIG_LWM2M_CARRIER */


