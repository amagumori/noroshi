#include <nrfx_pdm_ns.h>
#include <nrf9160.h>
#include <zephyr.h>
#include <gpio.h>

// https://devzone.nordicsemi.com/f/nordic-q-a/50656/using-pdm-mic-on-nrf9160dk

// NRFX_PDM_ENABLED needs to be set in nrfx_config_nrf9160.h

#define PDM_BUF_ADDRESS 0x20000000
#define PDM_BUF_SIZE 256  // 256 x 32bit words

void nrfx_pdm_event_handler( nrfx_pdm_evt_t *event ) {
  if ( event->buffer_requested ) {
    nrfx_pdm_buffer_set(pdm_buf, PDM_BUF_SIZE);
  }
  if ( event->buffer_released != 0 ) { 
    
  }
}
