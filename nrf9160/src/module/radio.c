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

#define MAX_MESSAGE_SIZE 21000  // 700 samples/s - 30 seconds
#define RADIO_MAX_MESSAGES 10   // max received messages in rx bfufer

#include "modules_common.h"
// #include "users.h"
#include "../events/app_event.h"
#include "../events/radio_event.h"
#include "../events/ui_event.h"

#include "../hw/mic.c"
#include "../hw/i2s.c"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, 1);   // log level 1 = ???

const k_tid_t radio_thread;

// I2S.h 
extern i16 *outgoing_message_buffer;
extern size_t outgoing_message_size;
extern u16 *incoming_message_buffer;
extern size_t incoming_message_size;
extern struct k_mutex incoming_mutex;
extern struct k_mutex outgoing_mutex;

/* 
 *
 * DATA LAYER - PACKET-LEVEL EVENTS
 *
 */

u16 mqtt_rx_buffer[CONFIG_MQTT_RX_BUFFER_SIZE];
u16 mqtt_tx_buffer[CONFIG_MQTT_TX_BUFFER_SIZE];

u16 tx_buffer[MAX_MESSAGE_SIZE];

u16 rx_buffer_front[MAX_MESSAGE_SIZE];
u16 rx_buffer_back[MAX_MESSAGE_SIZE];
bool front;


size_t audio_buffer_offset;
u16 *audio_ptr;

// MQTT_QOS_0_AT_MOST_ONCE
static mqtt_qos QOS = 0x00;
static struct mqtt_topic subscriptions[2];

#define MAX_MQTT_MESSAGE 4096
#define CHUNKED_MESSAGE_SIZE 4096   // set our chunk size here rather than using the max value

static struct mqtt_client client;
static struct sockaddr_storage broker;
static struct pollfd fds;

// literally do you even need any of this shit.

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

static int init_client( void ) {
  int err;

  // roll all the local init into its own helper function
  audio_buffer_offset = 0;
  front = true;

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
	client->rx_buf = mqtt_rx_buffer
	client->rx_buf_size = sizeof(mqtt_rx_buffer);
	client->tx_buf = mqtt_tx_buffer;
	client->tx_buf_size = sizeof(mqtt_tx_buffer);

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


void publish_radio( struct mqtt_client *client, u16 *data, size_t len ) {
  struct mqtt_publish_param param;

  param.message.topic.qos = QOS;
  param.message.topic.topic.utf8 = "radio";
	param.message.topic.topic.size = strlen("radio");
	param.message.payload.data = data;
	param.message.payload.len = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;

	data_print("Publishing: ", data, len);
	LOG_INF("to topic: %s", "radio")

	return mqtt_publish(client, &param);
}

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

int transmit( void ) {
  // MUTEX
  k_mutex_lock( &outgoing_mutex );

  int count = outgoing_message_size / CHUNKED_MESSAGE_SIZE;
  
  // @TODO this drops the last chunk in the buffer bc of division behavior.
  u16 *ptr = outgoing_message_buffer;
  for ( int i=0; i < count; i++ ) {
    publish( client, ptr, CHUNKED_MESSAGE_SIZE);
    ptr += CHUNKED_MESSAGE_SIZE;
  }

  k_mutex_unlock( &outgoing_mutex );
  return 1;
}

/* 
 * having sequenced transmissions is a whole other can of worms
 * that can be attempted later.  not minimum viable thing.
int read_payload ( struct mqtt_client *client, struct mqtt_publish_message *msg ) {
  u16 seq = msg->payload.seq;
  u16 offset = number * CONFIG_MQTT_PAYLOAD_BUFFER_SIZE;
  // uh oh
  memcpy(&output_buffer + offset, msg->payload.data, msg->payload.len); 
  message_chunks_remaining--;
  // this is absolutely not the way
}
*/

void switch_buffers( void ) {
  if ( audio_ptr >= rx_buffer_front &&
       audio_ptr <= rx_buffer_front + audio_buffer_offset ) {
    memset( rx_buffer_back, 0, MAX_MESSAGE_SIZE );
    audio_ptr = rx_buffer_back;
    audio_buffer_offset = 0;
    front = false;
  }
  if ( audio_ptr >= rx_buffer_back &&
       audio_ptr <= rx_buffer_back + audio_buffer_offset ) {
    memset( rx_buffer_front, 0, MAX_MESSAGE_SIZE );
    audio_ptr = rx_buffer_front;
    audio_buffer_offset = 0;
    front = true;
  }
  else {
    LOG_ERR("audio_ptr out of bounds. %x", audio_ptr);
    LOG_ERR("front buffer: %x to %x", rx_buffer_front, rx_buffer_front + audio_buffer_offset);
    LOG_ERR("back buffer: %x to %x", rx_buffer_back, rx_buffer_back + audio_buffer_offset);
  }
}

// so this is just throwing our things into a circular buffer
// which i think is the way
int push_audio_payload ( struct mqtt_client *client, struct mqtt_publish_message *msg ) {
  
  k_mutex_lock( &incoming_mutex );

  if ( audio_buffer_offset + msg->payload.len >= MAX_MESSAGE_SIZE ) {
    struct radio_event *evt = new_radio_event();
    evt->type = RADIO_EVENT_INCOMING_MSG_DONE;
    incoming_message_buffer = audio_ptr;
    incoming_message_size   = audio_buffer_offset;

    //evt->buffer_data.ptr = audio_ptr;
    //evt->buffer_data.len = audio_buffer_offset;

    switch_buffers();
    EVENT_SUBMIT( evt );
  }

  memcpy( &audio_ptr + audio_buffer_offset, msg->payload.data, msg->payload.len );
  audio_buffer_offset += msg->payload.len;

  if ( msg->payload.end == true ) {
    struct radio_event *evt = new_radio_event();
    evt->type = RADIO_EVENT_INCOMING_MSG_DONE;
    incoming_message_buffer = audio_ptr;
    incoming_message_size   = audio_buffer_offset;
    switch_buffers();
    EVENT_SUBMIT( evt );
  }

  k_mutex_unlock( &incoming_mutex );

  return 1;
}

void i2s_event_handler ( struct radio_msg *msg ) {
  if ( IS_EVENT ( msg, i2s, I2S_EVENT_TRANSMIT_READY ) ) {
    transmit();
  }
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
      push_audio_payload( client, p.message );
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

K_THREAD_DEFINE(radio_thread, RADIO_THREAD_STACK_SIZE, 
                entry_point, NULL, NULL, NULL,
                PRIORITY, 0, 0 );

#endif
