#ifndef _HW_EVENT_H
#define _HW_EVENT_H

#include "../types.h"
#include <event_manager.h>
#include <event_manager_profiler.h>

enum i2s_event_type {
  I2S_EVENT_PLAYING,
  I2S_EVENT_DONE_PLAYING,
  I2S_EVENT_TX_BUFFER_READY,
  I2S_EVENT_ERROR
};

struct i2s_event {
  struct event_header header;
  enum i2s_event_type type;

  union {
    u32 id;
  } data;
};

EVENT_TYPE_DECLARE( i2s_event );

#endif
