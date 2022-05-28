#include <zephyr.h>
#include <stdio.h>
#include <event_manager.h>
#include <settings/settings.h>
#include <date_time.h>
#include <modem/modem_info.h>
#include <net/mqtt.h>
#include <net/socket.h>
#include <kernel.h> // mutex

// CERTIFICATES GO HERE LOL
// #include "certificates/certs.h"

#include "../codec/codec.h"
#include "modem.h"

#include "modules_common.h"
// #include "users.h"
#include "../events/app_event.h"
#include "../events/radio_event.h"
#include "../events/ui_event.h"

// @TODO wrong includes
#include "../hw/mic.c"
#include "../hw/i2s.c"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, 1);   // log level 1 = ???

const k_tid_t radio_thread;

extern struct k_msgq *p_opus_outgoing;
extern struct k_msgq *p_opus_incoming;
const int DroppedPackets;

// MQTT_QOS_0_AT_MOST_ONCE
static mqtt_qos QOS = 0x00;
static struct mqtt_topic subscriptions[2];

static struct mqtt_client client;
static struct sockaddr_storage broker;
static struct pollfd fds;

struct m_audio_frame_t *tx_frame;
struct m_audio_frame_t *rx_frame;
struct mqtt_publish_param tx_publish_param;


struct radio_msg {
  union {
    struct app_event   app;
    struct radio_event radio;
    struct ui_event    ui;
    struct i2s_event   i2s;
  } module;
};

#define DATA_QUEUE_ENTRY_COUNT 10
#define DATA_QUEUE_ALIGNMENT 4
K_MSGQ_DEFINE(radio_msg_queue, sizeof(struct radio_msg), DATA_QUEUE_ENTRY_COUNT, DATA_QUEUE_ALIGNMENT);


/*
 * The idea as it stands right now:
 * 
 * TRANSMIT
 *
 * When you depress the push-to-talk button, you issue a "key-up request" to the broker.
 * The broker either:
 *   accepts your key-up, giving an ACK back; now every other publish message other than from you will be thrown away broker-side until you release it.
 *     (and new key-up requests will have an ACK DENY sent back.)
 *   denies your key-up.  this fires an event reflected in UI update, showing that the airwaves are keyed up by someone else right now.
 *
 * Once you're keyed up, the flow is:
 * I2S LINE IN / MIC -> OPUS ENCODER -> the p_opus_outgoing message queue.
 * each message in the queue is an encoder frame.
 * as long as there are remaining frames, the main thread sends MQTT PUBLISHes with the frames as payload.
 * as it stands now this is a high volume of MQTT publishes, although i think it'll be fine.
 *
 * --
 *
 * RECEIVE 
 *
 * when new PUBLISH events are received, it's handled in the MQTT event handler.
 * as of now - we issue mqtt_read_publish_payload directly into the rx_buffer ( which should match m_audio_frame_t as it is a frame.. )
 * we then push into p_opus_incoming message queue from rx_buffer.
 *
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

static int init_client( void ) {
  int err;

  DroppedPackets = 0;
  tx_frame = &frame_OPUS_encode;

  tx_publish_param = {
    .message.topic.qos = QOS,
    .message.topic.topic.utf8 = "radio",
    .message.topic.topic.size = strlen("radio"),
    .message.payload.data = tx_frame,
    .message.payload.len  = sizeof(tx_frame),
    .message_id = sys_rand32_get(),
    .dup_flag = 0,
    .retain_flag = 0
  };


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
  subscriptions[0] = {
    .topic = {
      .utf8 = "radio",
      .size = strlen("radio")
    },
    .qos = QOS
  };
  subscriptions[1] = {
    .topic = {
      .utf8 = "info",
      .size = strlen("info")
    },
    .qos = MQTT_QOS_1_AT_LEAST_ONCE
  };

  /*
  struct mqtt_topic sub_topic = {
    .topic = {
      .utf8 = CONFIG_MQTT_SUB_TOPIC,
      .size = strlen(CONFIG_MQTT_SUB_TOPIC)
    },
    .qos = QOS
  };
  */
  const struct mqtt_subscription_list sub_list = {
    .list = &subscriptions,
    .list_count = 2,
    .message_id = 1234 // 1234 means sub to both i suppose.
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


void publish_info( struct mqtt_client *client, u16 *data, size_t len ) {
  struct mqtt_publish_param param;

  param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
  param.message.topic.topic.utf8 = "info";
	param.message.topic.topic.size = strlen("info");
	param.message.payload.data = data;
	param.message.payload.len = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;

	data_print("Publishing: ", data, len);
	LOG_INF("to topic: %s.\n", "info");

	return mqtt_publish(client, &param);
}

void radio_loop() {
  int err;
  u32 connect_attempt = 0;
  LOG_INF("MQTT thread started.");

  // TLS stuff.

  do {
    err = modem_configure();
    if ( err ) {
      LOG_INF("retrying modem config in %d seconds.", CONFIG_LTE_CONNECT_RETRY_DELAY_S);
      k_sleep(K_SECONDS(CONFIG_LTE_CONNECT_RETRY_DELAY_S));
    } 
  } while ( err );

  err = init_client( &client );
  if ( err != 0 ) {
    LOG_ERR("init mqtt client: error %d", err);
    return;
  }

do_connect:
  if ( connect_attempt++ > 0 ) {
    LOG_INF("reconnecting in %d sec...", CONFIG_MQTT_RECONNECT_DELAY_S);
    k_sleep( K_SECONDS( CONFIG_MQTT_RECONNECT_DELAY_S ) );
  }
  err = mqtt_connect( &client );
  if ( err != 0 ) {
    LOG_ERR("mqtt connect: %d", err);
    goto do_connect;
  }

  err = fds_init( &client );
  if ( err != 0 ) {
    LOG_ERR("fds_init: %d", err);
    return;
  }

  while ( 1 ) {
    // keepalive ping
    err = poll(&fds, 1, mqtt_keepalive_time_left(&client));

    // poll codec message queue to see if TX frames are ready.
    if ( k_msgq_num_used_get( p_opus_outgoing) ) {
      k_msgq_get( &p_opus_outgoing, tx_frame, K_NO_WAIT );
      int err = mqtt_publish( client, tx_publish_param );
      if ( err ) {
        LOG_ERR("mqtt_publish error: %d", err );
        break;
      }
    }

    if ( err != 0 ) {
      LOG_ERR("poll: %d", err);
      break;
    }
    err = mqtt_live( &client );
    if ( err != 0 && ( err != -EAGAIN ) ) {
      LOG_ERR("mqtt_live: %d", err);
      break;
    }

    if ( ( fds.revents & POLLIN ) == POLLIN ) {
      err = mqtt_input(&client);
      if ( err != 0 ) {
        LOG_ERR("mqtt_input: %d", err);
        break;
      }
    }
    if ( ( fds.revents & POLLERR ) == POLLERR ) {
      LOG_ERR("mqtt_input: %d", err);
      break;
    }
    if ( ( fds.revents & POLLNVAL ) == POLLNVAL ) {
      LOG_ERR("mqtt_input: %d", err);
      break;
    }
  }

  LOG_INF("disconnecting mqtt client...");
  err = mqtt_disconnect(&client);
  if ( err ) {
    LOG_ERR("couldn't disconnect?! %d", err);
  }
  goto do_connect;
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

	case MQTT_EVT_PUBLISH:
		const struct mqtt_publish_param *p = &evt->param.publish;

		LOG_INF("MQTT PUBLISH received.");

    if ( strcmp( p.topic, "radio" ) == 0 ) {
      err = mqtt_read_publish_payload( &client, rx_frame, sizeof(struct m_audio_frame_t));
      if ( k_msgq_put( &p_opus_incoming, rx_frame, K_NO_WAIT ) != 0 ) {
        LOG_INF("dropped a payload. oopsie.");
        DroppedPackets++;
      }
    }

    if ( err != 0 ) {
      LOG_ERR("error reading published payload: %d", err);
    }
  
    break;

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

K_THREAD_DEFINE(radio_thread, RADIO_THREAD_STACK_SIZE, 
                radio_loop, NULL, NULL, NULL,
                PRIORITY, 0, 0 );

#endif
