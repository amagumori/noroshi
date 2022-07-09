#ifndef _AUDIO_EVENT_H
#define _AUDIO_EVENT_H

#include "../types.h"
#include <event_manager.h>
#include <event_manager_profiler.h>

enum audio_event_type {
  AUDIO_EVENT_PDM_BUFFER_READY,
  AUDIO_EVENT_I2S_PLAYING,
  AUDIO_EVENT_I2S_DONE_PLAYING,
  AUDIO_EVENT_ERROR
};

struct audio_event {
  struct event_header header;
  enum audio_event_type type;

  union {
    u32 id;
    // baaaad naming
    struct button_data button;
    // and more!
  } data;
};

EVENT_TYPE_DECLARE( audio_event );

#endif
