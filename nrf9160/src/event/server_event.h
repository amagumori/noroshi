#ifndef _SERVER_EVENT_H
#define _SERVER_EVENT_H

#include "../types.h"
#include <event_manager.h>
#include <event_manager_profiler.h>

enum server_event_type {
  SERVER_EVENT_BEGIN_AUTH,
  SERVER_EVENT_AUTH_DENIED,
  SERVER_EVENT_UNREACHABLE,
  SERVER_EVENT_CONNECTING,
  SERVER_EVENT_CONNECTED,
  SERVER_EVENT_DISCONNECTED,
  SERVER_EVENT_TIMEOUT,   // but this should never happenenenenn
  RADIO_EVENT_ERR
};

struct server_data {
  char *buffer;
  size_t len;
};

struct server_event {
  struct event_header header;
  enum server_event_type type;

  union {
    u32 id;
    int err;
    struct server_data data;
    // and more!
  } data;
};

EVENT_TYPE_DECLARE( server_event );

#endif
