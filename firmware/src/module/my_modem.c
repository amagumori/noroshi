#include <zephyr.h>
#include <stdio.h>
#include <stdio.h>
#include <event_manager.h>
#include <math.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <modem/at_cmd.h>
#include <modem/at_notif.h>

#include "../events/app_event.h"
#include "../events/radio_event.h"  // encoded audio tx's
#include "../events/modem_event.h"

#ifdef CONFIG_LWM2M_CARRIER
#include <lwm2m_carrier.h>
#include "../certs/carrier_certs.h"
#endif

struct modem_data {
  union {
    struct app_event app;
    struct radio_event radio;
    struct modem_event modem;
  } module_event;
};

static enum modem_state {
  STATE_INITIALISING,
  STATE_DISCONNECTED,
  STATE_CONNECTING,
  STATE_CONNECTED,
  STATE_SHUTDOWN,
} state;

static struct modem_param_info modem_param;
