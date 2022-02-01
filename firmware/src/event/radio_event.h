#ifndef _RADIO_EVENT_H
#define _RADIO_EVENT_H

#include "../types.h"
#include <event_manager.h>
#include <event_manager_profiler.h>

enum radio_event_type {
  RADIO_EVENT_INCOMING_MSG_START,
  RADIO_EVENT_INCOMING_MSG_DONE,
  RADIO_EVENT_ERR
};

struct radio_data {
  bool  end;
  char *buffer;
  size_t len;
};

struct buffer_data {
  char *ptr;
  size_t len;
};

struct radio_event {
  struct event_header header;
  enum radio_event_type type;

  union {
    u32 id;
    int err;
    struct radio_data data;
    struct buffer_data buffer;
    // and more!
  } data;
};

EVENT_TYPE_DECLARE( radio_event );

#endif
