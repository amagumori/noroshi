#ifndef _RADIO_EVENT_H
#define _RADIO_EVENT_H

#include "../types.h"
#include <event_manager.h>
#include <event_manager_profiler.h>

enum radio_event_type {
  RADIO_EVENT_INCOMING_MSG,
  RADIO_EVENT_ERR
};

struct radio_data {
  char *buffer;
  size_t len;
};

struct radio_event {
  struct event_header header;
  enum radio_event_type type;

  union {
    u32 id;
    int err;
    struct radio_data data;
    // and more!
  } data;
};

EVENT_TYPE_DECLARE( radio_event );

#endif
