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

static int configure_low_power(void)
{
	int err;

#if defined(CONFIG_UDP_PSM_ENABLE)
	/** Power Saving Mode */
	err = lte_lc_psm_req(true);
	if (err) {
		printk("lte_lc_psm_req, error: %d\n", err);
	}
#else
	err = lte_lc_psm_req(false);
	if (err) {
		printk("lte_lc_psm_req, error: %d\n", err);
	}
#endif

#if defined(CONFIG_UDP_EDRX_ENABLE)
	/** enhanced Discontinuous Reception */
	err = lte_lc_edrx_req(true);
	if (err) {
		printk("lte_lc_edrx_req, error: %d\n", err);
	}
#else
	err = lte_lc_edrx_req(false);
	if (err) {
		printk("lte_lc_edrx_req, error: %d\n", err);
	}
#endif

#if defined(CONFIG_UDP_RAI_ENABLE)
	/** Release Assistance Indication  */
	err = lte_lc_rai_req(true);
	if (err) {
		printk("lte_lc_rai_req, error: %d\n", err);
	}
#endif

	return err;
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

  SEND_EVENT(modem, MODEM_EVENT_LTE_CONNECTING);
  return 0;
}

static void rsrp_handler( char rsrp_val ) {
  if ( rsrp_val > 97 ) {
    return;
  }

  rsrp_value_latest = rsrp_to_dbm(rsrp_val);
  LOG_DEBUG("new RSRP value: %d", rsrp_value_latest);
}

static inline int rsrp_to_dbm(int input) {
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

static void neighbor_cell_update( struct lte_lc_cells_info *cell_info ) {
  struct modem_event *evt = new_modem_event();

  // asserts
  //
  memcpy( &evt->data.neighbor_cells.cell_data, cell_info, sizeof(struct lte_lc_cells_info) );
  memcpy( &evt->data.neighbor_cells.neighbor_cells, cell_info->neighbor_cells, 
    sizeof( struct lte_lc_ncell ) * evt->data.neighbor_cells.cell_data.ncells_count );
  // convert RSRP to dBm / RSRQ to dB
  evt->data.neighbor_cells.cell_data.current_cell.rsrp = rsrp_to_dbm(
      evt->data.neighbor_cells.cell_data.current_cell.rsrp );
  evt->data.neighbor_cells.cell_data.current_cell.rsrq = rsrq_to_db( 
      evt->data.neighbor_cells.cell_data.current_cell.rsrq );

  for ( size_t i=0; i < evt->data.neighbor_cells.cell_data.ncells_count; i++ ) {
    evt->data.neighbor_cells.neighbor_cells[i].rsrp = 
      rsrp_to_dbm( evt->data.neighbor_cells.neighbor_cells[i].rsrp );
    evt->data.neighbor_cells.neighbor_cells[i].rsrq = 
      rsrq_to_db( evt->data.neighbor_cells.neighbor_cells[i].rsrq );
  }

  evt->type = MODEM_EVENT_NEIGHBOR_CELLS_DATA_READY;
  evt->data.neighbor_cells.timestamp = k_uptime_get();

  EVENT_SUBMIT(evt);
}

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
	if (IS_EVENT(msg, modem, MODEM_EVENT_CARRIER_INITIALIZED)) {
		int err;

		state_set(STATE_DISCONNECTED);

		err = at_cmd_init();
		__ASSERT(err == 0, "Failed initializing at_cmd");

		err = at_notif_init();
		__ASSERT(err == 0, "Failed initializing at_notif");

		err = setup();
		__ASSERT(err == 0, "Failed running setup()");

		SEND_EVENT(modem, MODEM_EVENT_INITIALIZED);
	}
}

static void on_state_disconnected(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVENT_LTE_CONNECTED)) {
		state_set(STATE_CONNECTED);
	}

	if (IS_EVENT(msg, modem, MODEM_EVENT_LTE_CONNECTING)) {
		state_set(STATE_CONNECTING);
	}
}

static void on_state_connecting(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVENT_LTE_DISCONNECT)) {
		int err;

		err = lte_lc_offline();
		if (err) {
			LOG_ERR("LTE disconnect failed, error: %d", err);
			SEND_ERROR(modem, MODEM_EVENT_ERROR, err);
			return;
		}

		state_set(STATE_DISCONNECTED);
	}

	if (IS_EVENT(msg, modem, MODEM_EVENT_LTE_CONNECTED)) {
		state_set(STATE_CONNECTED);
	}
}

static void on_state_connected(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVENT_LTE_DISCONNECTED)) {
		state_set(STATE_DISCONNECTED);
	}
}

static void on_all_states(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVENT_START)) {
		int err;

		if (IS_ENABLED(CONFIG_LWM2M_CARRIER)) {

			return;
		}

		err = lte_connect();
		if (err) {
			LOG_ERR("Failed connecting to LTE, error: %d", err);
			SEND_ERROR(modem, MODEM_EVENT_ERROR, err);
			return;
		}
    SEND_EVENT(modem, MODEM_EVENT_LTE_CONNECTED);
	}

	if (IS_EVENT(msg, app, APP_EVENT_DATA_GET)) {

    SEND_EVENT(modem, MODEM_EVENT_LTE_CONNECTED);
		if (static_modem_data_requested(msg->module.app.data_list,
						msg->module.app.count)) {

			int err;

			err = static_modem_data_get();
			if (err) {
				SEND_EVENT(modem,
					MODEM_EVENT_MODEM_STATIC_DATA_NOT_READY);
			}
		}

		if (dynamic_modem_data_requested(msg->module.app.data_list,
						 msg->module.app.count)) {

			int err;

			err = dynamic_modem_data_get();
			if (err) {
				SEND_EVENT(modem,
					MODEM_EVENT_MODEM_DYNAMIC_DATA_NOT_READY);
			}
		}

		if (battery_data_requested(msg->module.app.data_list,
					   msg->module.app.count)) {

			int err;

			err = battery_data_get();
			if (err) {
				SEND_EVENT(modem,
					MODEM_EVENT_BATTERY_DATA_NOT_READY);
			}
		}

		if (neighbor_cells_data_requested(msg->module.app.data_list,
						  msg->module.app.count)) {
			int err;

			err = neighbor_cells_measurement_start();
			if (err) {
				SEND_EVENT(modem, MODEM_EVENT_NEIGHBOR_CELLS_DATA_NOT_READY);
			}
		}
	}

	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		lte_lc_power_off();
		state_set(STATE_SHUTDOWN);
		SEND_SHUTDOWN_ACK(modem, MODEM_EVENT_SHUTDOWN_READY, self.id);
	}
}

static void entry_point(void)
{
	int err;
	struct modem_msg msg;

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(modem, MODEM_EVENT_ERROR, err);
	}

	if (IS_ENABLED(CONFIG_LWM2M_CARRIER)) {
		state_set(STATE_INIT);
	} else {
		state_set(STATE_DISCONNECTED);
		SEND_EVENT(modem, MODEM_EVENT_INITIALIZED);

		err = setup();
		if (err) {
			LOG_ERR("Failed setting up the modem, error: %d", err);
			SEND_ERROR(modem, MODEM_EVENT_ERROR, err);
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
