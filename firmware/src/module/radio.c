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

// CERTIFICATES GO HERE LOL
// #include "certificates/certs.h"

#include "../codec/codec.h"

#define AUDIO_BUFFER_SIZE 2048  // man i dunno
#define MODEM_BUFFER_SIZE 2048  // man i dunno
#define MAX_RADIO_MESSAGE_SIZE 21000  // 700 samples/s - 30 seconds
#define RADIO_MAX_MESSAGES 10   // max received messages in rx bfufer

#include "modules_common.h"
// #include "users.h"
#include "../events/app_event.h"
#include "../events/radio_event.h"
#include "../events/modem_event.h"
#include "../events/ui_event.h"

#include "../hw/mic.c"

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

/* 
 *
 * DATA LAYER - PACKET-LEVEL EVENTS
 *
 */

static uint16_t rx_buffer[CONFIG_MQTT_RECV_BUFFER_SIZE];
static uint16_t tx_buffer[CONFIG_MQTT_TX_BUFFER_SIZE];
static uint16_t payload_buffer[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

// MQTT_QOS_0_AT_MOST_ONCE
static mqtt_qos QOS = 0x00;

static struct mqtt_client client;
static struct sockaddr_storage broker;
static struct pollfd fds;

// literally do you even need any of this shit.

struct data_msg {
  union {
    struct modem_event modem;
    struct app_event app;
    struct radio_event radio;
  } module;
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

/*
 
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

*/

// completely drop the asset_tracker style event approach.
// instead write the handlers for MQTT events and extend it later IF needed.
// still use the kernel message Q tho


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

/*
 * MQTT STUFF 
 *
 */

static int init_broker( void ) {
  int err;
  struct addrinfo *result;
  struct addrinfo *addr;
  struct addrinfo hints = {
    .ai_family = AF_INET,
    .ai_socktype = SOCK_STREAM
  };

  err = getaddrinfo( CONFIG_MQTT_BROKER_HOSTNAME, NULL, &hints, &result);
  if ( err ) {
    // log the err
    return -ECHILD;
  }

  addr = result;

  while ( addr != NULL ) {
    
    if ( addr->ai_addrlen == sizeof( struct sockaddr_in ) ) {
      struct sockaddr_in *broker4 = 
        ( ( struct sockaddr_in * )&broker );
      char ipv4_addr[NET_IPV4_ADDR_LEN];

      broker4->sin_addr.s_addr = 
        ( ( struct sockaddr_in * )addr->ai_addr)->sin_addr.s_addr;    // jesus christ
      broker4->sin_family = AF_INET;
      broker4->sin_port = htons( CONFIG_MQTT_BROKER_PORT );

      inet_ntop(AF_INET, &broker4->sin_addr.s_addr,
          ipv4_addr, sizeof(ipv4_addr));
      // LOG_INF("ipv4 address found: %s", log_strdup(ipv4_addr));
      break;

    } else {
      
      // LOG_ERR - reference line 337
    }

    addr = addr->ai_next;
  }

  freeaddrinfo(result);

  return err;

}

static int init_client( struct mqtt_client *client ) {
  int err;

  mqtt_client_init(client);

  err = broker_init();
  if ( err ) {
    // log error
    return err;
  }

  // check if mic / codec is setup

  if ( is_initialized() != true ) {
    // LOG_ERR
  }

  client->broker = &broker;
  client->evt_cb = mqtt_event_handler;
  client->client_id.utf8 = get_client_id();
  client->client_id.size = strlen(client->client_id.utf8);
  client->password = NULL;
  client->user_name = NULL;
  client->protocol_version = MQTT_VERSION_3_1_1;

 	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
#if defined(CONFIG_MQTT_LIB_TLS)
	struct mqtt_sec_config *tls_cfg = &(client->transport).tls.config;
	static sec_tag_t sec_tag_list[] = { CONFIG_MQTT_TLS_SEC_TAG };

	LOG_INF("TLS enabled");
	client->transport.type = MQTT_TRANSPORT_SECURE;

	tls_cfg->peer_verify = CONFIG_MQTT_TLS_PEER_VERIFY;
	tls_cfg->cipher_count = 0;
	tls_cfg->cipher_list = NULL;
	tls_cfg->sec_tag_count = ARRAY_SIZE(sec_tag_list);
	tls_cfg->sec_tag_list = sec_tag_list;
	tls_cfg->hostname = CONFIG_MQTT_BROKER_HOSTNAME;

#if defined(CONFIG_NRF_MODEM_LIB)
	tls_cfg->session_cache = IS_ENABLED(CONFIG_MQTT_TLS_SESSION_CACHING) ?
					    TLS_SESSION_CACHE_ENABLED :
					    TLS_SESSION_CACHE_DISABLED;
#else
	/* TLS session caching is not supported by the Zephyr network stack */
	tls_cfg->session_cache = TLS_SESSION_CACHE_DISABLED;

#endif

#else
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;
#endif

	return err;
}

static int fds_init(struct mqtt_client *c)
{
	if (c->transport.type == MQTT_TRANSPORT_NON_SECURE) {
		fds.fd = c->transport.tcp.sock;
	} else {
#if defined(CONFIG_MQTT_LIB_TLS)
		fds.fd = c->transport.tls.sock;
#else
		return -ENOTSUP;
#endif
	}

	fds.events = POLLIN;

	return 0;
}

static int subscribe( void ) {
  struct mqtt_topic sub_topic = {
    .topic = {
      .utf8 = CONFIG_MQTT_SUB_TOPIC,
      .size = strlen(CONFIG_MQTT_SUB_TOPIC)
    },
    .qos = QOS
  };

  const struct mqtt_subscription_list sub_list = {
    .list = &sub_topic,
    .list_count = 1,
    .message_id = 1234 // ???
  };

  // LOG( subbing to: "

  return mqtt_subscribe(&client, &sub_list);
}

/* 
 * ok so i think the idea is:
 *
 * first message has a header containing total seq count (number of messages / chunks).
 *
 * this is transmitted with QOS 1 so you ack it.
 *
 * after that you straight up just throw messages into the void
 * if you get em you get em if you don't you dont.
 *
 * fill the speaker buffer as the messages come in - once you receive the last packet
 * in the sequence, fill what you didn't receive with silence (?) and call the interrupt (???)
 *
 */


void publish( struct mqtt_client *client, u16 *data, size_t len ) {
  struct mqtt_publish_param param;

  param.message.topic.qos = QOS;
  param.message.topic.topic.utf8 = CONFIG_MQTT_PUB_TOPIC;
	param.message.topic.topic.size = strlen(CONFIG_MQTT_PUB_TOPIC);
	param.message.payload.data = data;
	param.message.payload.len = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;

	data_print("Publishing: ", data, len);
	LOG_INF("to topic: %s len: %u",
		CONFIG_MQTT_PUB_TOPIC,
		(unsigned int)strlen(CONFIG_MQTT_PUB_TOPIC));

	return mqtt_publish(client, &param);
}

// so this way we'll 
void publish_message_header( struct mqtt_client *client, msg_header *msg, size_t len ) {
  struct mqtt_publish_param param;

  param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
  param.message.topic.topic.utf8 = CONFIG_MQTT_PUB_TOPIC;
	param.message.topic.topic.size = strlen(CONFIG_MQTT_PUB_TOPIC);
  // this probably doesn't work.
	param.message.payload.data = (u8 *) data;
	param.message.payload.len  = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;
 
}

// when in doubt, do it the dumbest way possible
void publish_sequenced( struct mqtt_client *client, u16 *data, size_t len, u16 seq ) {
  struct mqtt_publish_param param;

  param.message.topic.qos = QOS;
  param.message.topic.topic.utf8 = CONFIG_MQTT_PUB_TOPIC;
	param.message.topic.topic.size = strlen(CONFIG_MQTT_PUB_TOPIC);
	param.message.payload.data = data;
	param.message.payload.len  = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;

	data_print("Publishing: ", data, len);
	LOG_INF("to topic: %s len: %u",
		CONFIG_MQTT_PUB_TOPIC,
		(unsigned int)strlen(CONFIG_MQTT_PUB_TOPIC));

	return mqtt_publish(client, &param);
}

int transmit( u16 *mic_buffer, size_t len ) {
  u16 *p = mic_buffer;
  u16 seq = 0;

  size_t chunk_size = CONFIG_MQTT_PAYLOAD_BUFFER_SIZE;

  // we want to extend mqtt_param->payload struct to include a packet number to do in-order

  while ( p < len ) {
    publish_sequenced( client, mic_buffer, chunk_size, seq );
    p += chunk_size;
    seq++;
  }
}

int read_payload ( struct mqtt_client *client, struct mqtt_publish_message *msg ) {
  u16 seq = msg->payload.seq;
  u16 offset = number * CONFIG_MQTT_PAYLOAD_BUFFER_SIZE;
  // uh oh
  memcpy(&output_buffer + offset, msg->payload.data, msg->payload.len); 
  message_chunks_remaining--;
  // this is absolutely not the way
}

void mqtt_event_handler( struct mqtt_client *client, struct mqtt_evt *evt ) {
  int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed: %d", evt->result);
			break;
		}

		LOG_INF("MQTT client connected");
		subscribe();
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected: %d", evt->result);
		break;

	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p = &evt->param.publish;

		LOG_INF("MQTT PUBLISH result=%d len=%d",
			evt->result, p->message.payload.len);
		err = publish_get_payload(c, p->message.payload.len);

    if ( p.user_hash != me ) {
      read_payload( client, p.message );
    }

    /*
		if (err >= 0) {
			data_print("Received: ", payload_buf,
				p->message.payload.len);
			* Echo back received data 
			data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE,
				payload_buf, p->message.payload.len);
		} else {
      */ 
			LOG_ERR("publish_get_payload failed: %d", err);
			LOG_INF("Disconnecting MQTT client...");

			err = mqtt_disconnect(c);
			if (err) {
				LOG_ERR("Could not disconnect: %d", err);
			}
		
	} break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error: %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT SUBACK error: %d", evt->result);
			break;
		}

		LOG_INF("SUBACK packet id: %u", evt->param.suback.message_id);
		break;

	case MQTT_EVT_PINGRESP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PINGRESP error: %d", evt->result);
		}
		break;

	default:
		LOG_INF("Unhandled MQTT event type: %d", evt->type);
		break;
	}
}
