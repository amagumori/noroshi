#include <zephyr.h>
#include <stdio.h>
#include <net/mqtt.h>
#include <net/socket.h>

static uint16_t rx_buffer[CONFIG_MQTT_RECV_BUFFER_SIZE];
static uint16_t tx_buffer[CONFIG_MQTT_TX_BUFFER_SIZE];
static uint16_t payload_buffer[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

// MQTT_QOS_0_AT_MOST_ONCE
static mqtt_qos QOS = 0x00;

static struct mqtt_client client;
static struct sockaddr_storage broker;
static struct pollfd fds;

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
