

#ifndef _APP_MODULE_EVENT_H_
#define _APP_MODULE_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Event types submitted by Application module. */
enum app_module_event_type {
	/** Signal that the application has done necessary setup, and
	 *  now started.
	 */
	APP_EVENT_START,

	/** Connect to LTE network. */
	APP_EVENT_LTE_CONNECT,

	/** Disconnect from LTE network. */
	APP_EVENT_LTE_DISCONNECT,

	/** Signal other modules to start sampling and report the data when
	 *  it's ready
	 *  The event must also contain a list with requested data types,
	 *  @ref app_module_data_type.
	 */
	APP_EVENT_SERVER_CONNECT_START,
  APP_EVENT_SERVER_CONNECT_SUCCESS,
  APP_EVENT_SERVER_CONNECT_FAIL,

  APP_EVENT_SERVER_AUTH_START,
  APP_EVENT_SERVER_AUTH_SUCCESS,
  APP_EVENT_SERVER_AUTH_FAIL,

  APP_EVENT_TALKGROUP_JOIN,
  APP_EVENT_TALKGROUP_JOIN_SUCCESS,
  APP_EVENT_TALKGROUP_JOIN_FAIL,

  APP_EVENT_STATION_CONNECT,
  APP_EVENT_STATION_CONNECT_SUCCESS,
  APP_EVENT_STATION_CONNECT_FAIL,

  APP_EVENT_TALK_START,
  APP_EVENT_TALK_END,

  APP_EVENT_LISTEN_START,
  APP_EVENT_LISTEN_END,
	APP_EVENT_SHUTDOWN_READY,

	/** An irrecoverable error has occurred in the application module. Error details are
	 *  attached in the event structure.
	 */
	APP_EVENT_ERROR
};

/** @brief Data types that the application module requests samples for in
 *	   @ref app_module_event_type APP_EVENT_DATA_GET.
 */
enum app_module_data_type {

	APP_DATA_SERVER,
  APP_DATA_TALKGROUP,
  APP_DATA_STATION,
  APP_DATA_TALKER,


	APP_DATA_MODEM_STATIC,
	APP_DATA_MODEM_DYNAMIC,
	APP_DATA_BATTERY,
	APP_DATA_NEIGHBOR_CELLS,

	APP_DATA_COUNT,
};

/** @brief Application module event. */
struct app_module_event {
	struct app_event_header header;
	enum app_module_event_type type;
	enum app_module_data_type data_list[APP_DATA_COUNT];

	union {
		/** Code signifying the cause of error. */
		int err;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
	} data;

	size_t count;

	/** The time each module has to fetch data before what is available
	 *  is transmitted.
	 */
	int timeout;
};

/** Register app module events as an event type with the Application Event Manager. */
APP_EVENT_TYPE_DECLARE(app_module_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _APP_MODULE_EVENT_H_ */
