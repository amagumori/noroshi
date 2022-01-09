
#include <zephyr.h>
#include <event_manager.h>
#include "../types.h"

enum ui_input_type {
  POWER_BUTTON,
  PUSH_BUTTON
  // and many more
};

struct ui_input_event {
  struct event_header header;
  enum ui_input_type type;
  u8 device_number;
  bool state; // this can only be bool for buttons...
};

EVENT_TYPE_DECLARE(ui_input_event);
