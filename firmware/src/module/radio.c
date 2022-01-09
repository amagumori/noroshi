#include <zephyr.h>
#include <event_manager.h>
#include <settings/settings.h>
#include <date_time.h>
#include <modem/modem_info.h>
#include "../codec/codec.h"

#define AUDIO_BUFFER_SIZE 2048  // man i dunno
#define MODEM_BUFFER_SIZE 2048  // man i dunno

#include "modules_common.h"
#include "../events/app_event.h"
#include "../events/radio_event.h"
#include "../events/modem_event.h"
#include "../events/ui_event.h"

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

// RING BUFFERS

static struct audio_data audio_buffer[AUDIO_BUFFER_SIZE];
static struct modem_data modem_buffer[MODEM_BUFFER_SIZE];
static int audio_buffer_head;
static int modem_buffer_head;

#define DATA_QUEUE_ENTRY_COUNT 10
#define DATA_QUEUE_ALIGNMENT 4
K_MSGQ_DEFINE(msgq_data, sizeof(struct data_msg), DATA_QUEUE_ENTRY_COUNT, DATA_QUEUE_ALIGNMENT);

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
