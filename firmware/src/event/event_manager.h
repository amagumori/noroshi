#ifndef _APP_EVENT_MANAGER_H
#define _APP_EVENT_MANAGER_H

#include <drivers/lte_lc.h>

static int configure_modem(void) {
  LOG_INF("disable PSM and eDRX");
  lte_lc_psm_req(false);
  lte_lc_edrx_req(false);

  if ( IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {

  } else {
#if defined(CONFIG_LWM2M_CARRIER)
    // wait for lwm2m carrier to configure modem and start connection.
    LOG_INF("waiting for carrier reg...");
    k_sem_take(&carrier_registered, K_FOREVER);
    LOG_INF("registered!")

#else
    int err;

    LOG_INF("LTE link connecting...");
    k_sem_take(&carrier_registered, K_FOREVER);
    LOG_INF("registered!")
#else 
    int err;
    LOG_INF("lte link connecting...");
    err = lte_lc_init_and_connect();
    if ( err ) {
      LOG_INF("failed to establish LTE connection: %d", err);
      return err;
    }
    LOG_INF("LTE link connected!")
#endif
  }
#endif
  return 0;
}


