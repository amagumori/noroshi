#ifndef _HW_EVENT_H
#define _HW_EVENT_H

#include "../types.h"
#include <event_manager.h>
#include <event_manager_profiler.h>

enum hw_event_type {
  HW_EVENT_PTT_DOWN,
  HW_EVENT_PTT_UP,
  HW_EVENT_VOLUME_POT_CHANGE,

  HW_EVENT_I2S_TRANSMIT_READY,
  HW_EVENT_I2S_PLAYING,
  HW_EVENT_I2S_DONE_PLAYING,
  HW_EVENT_ERROR
};

struct button_data {
  int id;
  u32 value;
};

struct hw_event {
  struct event_header header;
  enum hw_event_type type;

  union {
    u32 id;
    // baaaad naming
    struct button_data button;
    // and more!
  } data;
};

EVENT_TYPE_DECLARE( hw_event );

#endif
