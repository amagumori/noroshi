#ifndef _APP_EVENT_H
#define _APP_EVENT_H

#include "../types.h"
#include <event_manager.h>
#include <event_manager_profiler.h>

enum app_event_type {
  APP_EVENT_BOOT,
  APP_EVENT_CONNECTING,
  APP_EVENT_DISCONNECTED,
  APP_EVENT_INCOMING_MSG,
  APP_EVENT_LISTENING,  // actively playing audio
  APP_EVENT_TRANSMITTING,
  APP_EVENT_BEGIN_SHUTDOWN,
  APP_EVENT_ERROR
};

struct button_data {
  int id;
  u32 value;
};

struct app_event {
  struct event_header header;
  enum app_event_type type;

  union {
    int err;
    u32 id;
    // and more!
  } data;
};

EVENT_TYPE_DECLARE( app_event );

#endif
