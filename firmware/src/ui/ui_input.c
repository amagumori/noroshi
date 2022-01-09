#include <zephyr.h>
#include "../event/ui_input_event.h"

static void event_handler(u32 device_states, u32 has_changed ) {
  u8 device_number;

  while ( has_changed ) {
    device_number = 0;

    for (u8 i=0; i < 32; i++ ) {
      if ( has_changed & BIT(i) ) {
        device_number = i + 1;
        break;
      }
    }

    has_changed &= ~(1UL << (dev_num - 1));

    struct ui_input_event *event = new_ui_input_event();

    event->type = device_number > 2 ? POWER_BUTTON : PUSH_BUTTON;
    if ( device_number > 2 ) {
      event->device_number = ( dev_num % 3 ) + 1;
    } else {
      event->device_number = device_number;
    }
    event->state = ( device_states & BIT(device_number - 1) );

    EVENT_SUBMIT(event);
  }
}

int ui_input_init( void ) {
  static bool initialised;

  if ( !initialised ) {
    int ret = dk_buttons_init(event_handler);

    if ( ret ) {
      printf("couldn't initialize buttons (%d)", ret);
      return ret;
    }
    initialised = true;
  }
  return 0;
}
