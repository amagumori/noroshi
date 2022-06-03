#ifndef _MODEM_EVENT_H
#define _MODEM_EVENT_H

#include <event_manager.h>
#include <event_manager_profiler.h>


enum modem_event_type {
	MODEM_EVENT_INITIALIZED,
	MODEM_EVENT_LTE_CONNECTED,
	MODEM_EVENT_LTE_DISCONNECTED,
	MODEM_EVENT_LTE_CONNECTING,
	MODEM_EVENT_LTE_CELL_UPDATE,
	MODEM_EVENT_LTE_PSM_UPDATE,
	MODEM_EVENT_LTE_EDRX_UPDATE,
	MODEM_EVENT_MODEM_STATIC_DATA_READY,
	MODEM_EVENT_MODEM_DYNAMIC_DATA_READY,
	MODEM_EVENT_MODEM_STATIC_DATA_NOT_READY,
	MODEM_EVENT_MODEM_DYNAMIC_DATA_NOT_READY,
	MODEM_EVENT_NEIGHBOR_CELLS_DATA_READY,
	MODEM_EVENT_NEIGHBOR_CELLS_DATA_NOT_READY,
	MODEM_EVENT_BATTERY_DATA_NOT_READY,
	MODEM_EVENT_BATTERY_DATA_READY,
	MODEM_EVENT_SHUTDOWN_READY,
	MODEM_EVENT_ERROR,
	/** The carrier library has initialized the modem library and it is
	 *  now ready to be used. When the carrier library is enabled, this
	 *  event must be received before the modem module can proceed to initialize
	 *  other dependencies and subsequently send MODEM_EVENT_INITIALIZED.
	 */
	MODEM_EVENT_CARRIER_INITIALIZED,
	/** Due to modem limitations for active TLS connections, the carrier
	 *  library requires all other TLS connections in the system to
	 *  be terminated while FOTA update is ongoing.
	 */
	MODEM_EVENT_CARRIER_FOTA_PENDING,
	/** FOTA update has been stopped and the application can set up TLS
	 *  connections again.
	 */
	MODEM_EVENT_CARRIER_FOTA_STOPPED,
	/** The carrier library requests that the device reboots to apply
	 *  downloaded firmware image(s) or for other reasons.
	 */
	MODEM_EVENT_CARRIER_REBOOT_REQUEST,

};

struct modem_psm {
  int tau;
  int active_time;
}; 

struct modem_cell {
  // U-TRAN id
  u32 cell_id;
  // Tracking Area Code
  u32 tac;
};

struct modem_edrx {
  // edrx interval in s
  float edrx;
  // paging time window
  float ptw;
};

struct modem_battery {
  u16 voltage;
  i64 timestamp;
};

struct modem_neighbor_cells {
  struct lte_lc_cells_info cell_data;
  struct lte_lc_ncell neighbor_cells[17];
  i64 timestamp;
};

// modem static and dynamic data types conspicuously absent

struct modem_event {
  struct event_header header;
  enum modem_event_type type;
  union {
    struct modem_battery_data battery;
    struct modem_cell cell;
    struct modem_psm psm;
    struct modem_edrx edrx;
    struct modem_neighbor_cells neighbor_cells;
    u32 id;
    int err;
  } data;
};

EVENT_TYPE_DECLARE(modem_event);

#endif
