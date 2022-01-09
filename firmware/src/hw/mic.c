#include <nrfx_pdm_ns.h>
#include <nrf9160.h>
#include <zephyr.h>
#include <gpio.h>

#include "../types.h"

#define PDM_BUF_ADDRESS 0x20000000
#define PDM_BUF_SIZE 256
#define PIN_CLK 18
#define PIN_DIN 16

i16 buf1[1024];
i16 buf2[1024];

bool flag = 0;
bool WriteFlag = 0;
const nrfx_pdm_config_t config;

// rather than fwrite'ing to a block device we want to get a handle or pointer
// to a kernel ring buffer and slap PCM data in for Codec2 then eventually ARM
// hw-accelerated encryption 

static void drv_pdm_cb( const nrfx_pdm_evt_t *event ) {
  if ( event->buffer_requested ) {
    if ( !flag ) {
      i32 error = nrfx_pdm_buffer_set(&buf1[0], 1024);
      flag = 1;
      WriteFlag = 1;
    } else {
      i32 error = nrfx_pdm_buffer_set(&buf2[0], 1024);
      flag = 0;
      WriteFlag = 1;
    }
  }
}

int init_pdm( void ) {
  u32 error = 0;

  config = NRFX_PDM_DEFAULT_CONFIG(PIN_CLK, PIN_DIN);
  err = nrfx_pdm_init(&config, drv_pdm_cb);
  err = nrfx_pdm_start();
}

void nrfx_pdm_event_handler( nrfx_pdm_evt_t *event ) {
  if ( event->buffer_requested ) {
    nrfx_pdm_buffer_set(pdm_buf, PDM_BUF_SIZE);
  } 
  if ( event->buffer_released != 0 ) {
    
  }
}
