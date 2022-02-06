#include <zephyr.h>
#include <stdio.h>
#include <event_manager.h>
#include <settings/settings.h>
#include <date_time.h>
#include <modem/modem_info.h>
#include <net/mqtt.h>
#include <net/socket.h>

#if defined(CONFIG_MODEM_KEY_MGMT)
#include <modem/modem_key_mgmt.h>
#endif

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, 1);
/* 
 *
 * CERTIFICATE PROVISIONING
 *
 */

static int certificates_provision( void ) {
  int err = 0;

#if defined(CONFIG_NRF_MODEM_LIB) && defined(CONFIG_MODEM_KEY_MGMT)

  err = modem_key_mgmt_write(CONFIG_MQTT_TLS_SEC_TAG,
                             MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
                             CA_CERTIFICATE,
                             strlen(CA_CERTIFICATE));

  if ( err ) {
    // log the error lol
    return err;
  }

#elif defined(CONFIG_BOARD_QEMU_X86) && defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)

  err = tls_credential_add(CONFIG_MQTT_TLS_SEC_TAG,
                           TLS_CREDENTIAL_CA_CERTIFICATE,
                           CA_CERTIFICATE,
                           sizeof(CA_CERTIFICATE));

  if ( err ) {
    // log the error lol
    return err;
  }
#endif 
  return err;
}

static struct module_data self = {
  .name = "modem",
  .msg_q = &msgq_modem,
  .supports_shutdown = true,
};

struct modem_param_info modem_info;

static i16 rsrp_value_latest;
const k_tid_t modem_thread; 

struct modem_msg {
  union {
    struct app_event app;
    struct radio_event radio;
  } module;
};

static enum state_type {
  STATE_INIT,
  STATE_DISCONNECTED,
  STATE_CONNECTING,
  STATE_CONNECTED,
  STATE_SHUTDOWN,
} state;

#define DATA_QUEUE_ENTRY_COUNT 10
#define DATA_QUEUE_ALIGNMENT 4
K_MSGQ_DEFINE(radio_msg_queue, sizeof(struct modem_msg), DATA_QUEUE_ENTRY_COUNT, DATA_QUEUE_ALIGNMENT);

static void submit_cell_update(u32 cell_id, u32 tac);
static void submit_psm_update(int tau, int active_time);
static void submit_edrx_update(float edrx, float ptw);
//static void submit_neighbor_update(u32 cell_id, u32 tac);
static inline int rsrp_to_db( int input );
static inline int rsrq_to_db( int input );

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

/* @TODO
 *
 *
 * REFACTOR FROM HERE 
 *

static int modem_data_init(void)
{
	int err;

	err = modem_info_init();
	if (err) {
		LOG_INF("modem_info_init, error: %d", err);
		return err;
	}

	err = modem_info_params_init(&modem_param);
	if (err) {
		LOG_INF("modem_info_params_init, error: %d", err);
		return err;
	}

	err = modem_info_rsrp_register(modem_rsrp_handler);
	if (err) {
		LOG_INF("modem_info_rsrp_register, error: %d", err);
		return err;
	}

	return 0;
}

static int setup(void)
{
	int err;

	if (!IS_ENABLED(CONFIG_LWM2M_CARRIER)) {
		err = lte_lc_init();
		if (err) {
			LOG_ERR("lte_lc_init, error: %d", err);
			return err;
		}
	}

	if (IS_ENABLED(CONFIG_MODEM_AUTO_REQUEST_POWER_SAVING_FEATURES)) {
		err = configure_low_power();
		if (err) {
			LOG_ERR("configure_low_power, error: %d", err);
			return err;
		}
	}

	err = modem_data_init();
	if (err) {
		LOG_ERR("modem_data_init, error: %d", err);
		return err;
	}

	return 0;
}

static void on_state_init(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_CARRIER_INITIALIZED)) {
		int err;

		state_set(STATE_DISCONNECTED);

		err = at_cmd_init();
		__ASSERT(err == 0, "Failed initializing at_cmd");

		err = at_notif_init();
		__ASSERT(err == 0, "Failed initializing at_notif");

		err = setup();
		__ASSERT(err == 0, "Failed running setup()");

		SEND_EVENT(modem, MODEM_EVT_INITIALIZED);
	}
}

static void on_state_disconnected(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED)) {
		state_set(STATE_CONNECTED);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTING)) {
		state_set(STATE_CONNECTING);
	}
}

static void on_state_connecting(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_LTE_DISCONNECT)) {
		int err;

		err = lte_lc_offline();
		if (err) {
			LOG_ERR("LTE disconnect failed, error: %d", err);
			SEND_ERROR(modem, MODEM_EVT_ERROR, err);
			return;
		}

		state_set(STATE_DISCONNECTED);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED)) {
		state_set(STATE_CONNECTED);
	}
}

static void on_state_connected(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_DISCONNECTED)) {
		state_set(STATE_DISCONNECTED);
	}
}

static void on_all_states(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_START)) {
		int err;

		if (IS_ENABLED(CONFIG_LWM2M_CARRIER)) {

			return;
		}

		err = lte_connect();
		if (err) {
			LOG_ERR("Failed connecting to LTE, error: %d", err);
			SEND_ERROR(modem, MODEM_EVT_ERROR, err);
			return;
		}
	}

	if (IS_EVENT(msg, app, APP_EVT_DATA_GET)) {
		if (static_modem_data_requested(msg->module.app.data_list,
						msg->module.app.count)) {

			int err;

			err = static_modem_data_get();
			if (err) {
				SEND_EVENT(modem,
					MODEM_EVT_MODEM_STATIC_DATA_NOT_READY);
			}
		}

		if (dynamic_modem_data_requested(msg->module.app.data_list,
						 msg->module.app.count)) {

			int err;

			err = dynamic_modem_data_get();
			if (err) {
				SEND_EVENT(modem,
					MODEM_EVT_MODEM_DYNAMIC_DATA_NOT_READY);
			}
		}

		if (battery_data_requested(msg->module.app.data_list,
					   msg->module.app.count)) {

			int err;

			err = battery_data_get();
			if (err) {
				SEND_EVENT(modem,
					MODEM_EVT_BATTERY_DATA_NOT_READY);
			}
		}

		if (neighbor_cells_data_requested(msg->module.app.data_list,
						  msg->module.app.count)) {
			int err;

			err = neighbor_cells_measurement_start();
			if (err) {
				SEND_EVENT(modem, MODEM_EVT_NEIGHBOR_CELLS_DATA_NOT_READY);
			}
		}
	}

	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		lte_lc_power_off();
		state_set(STATE_SHUTDOWN);
		SEND_SHUTDOWN_ACK(modem, MODEM_EVT_SHUTDOWN_READY, self.id);
	}
}

* to here
*/

static void entry_point(void)
{
	int err;
	struct modem_msg msg;

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(modem, MODEM_EVT_ERROR, err);
	}

	if (IS_ENABLED(CONFIG_LWM2M_CARRIER)) {
		state_set(STATE_INIT);
	} else {
		state_set(STATE_DISCONNECTED);
		SEND_EVENT(modem, MODEM_EVT_INITIALIZED);

		err = setup();
		if (err) {
			LOG_ERR("Failed setting up the modem, error: %d", err);
			SEND_ERROR(modem, MODEM_EVT_ERROR, err);
		}
	}

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_INIT:
			on_state_init(&msg);
			break;
		case STATE_DISCONNECTED:
			on_state_disconnected(&msg);
			break;
		case STATE_CONNECTING:
			on_state_connecting(&msg);
			break;
		case STATE_CONNECTED:
			on_state_connected(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_WRN("Invalid state: %d", state);
			break;
		}

		on_all_states(&msg);
	}
}


K_THREAD_DEFINE(modem_thread, CONFIG_MODEM_THREAD_STACK_SIZE,
		entry_point, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE_EARLY(MODULE, radio_event);
EVENT_SUBSCRIBE(MODULE, hw_event );

#endif
