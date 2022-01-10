#include <nrfx_pdm_ns.h>
#include <nrf9160.h>
#include <zephyr.h>
#include <gpio.h>

#include "freedv_api.h"
#include "../types.h"

#define PDM_BUF_ADDRESS 0x20000000
#define PDM_BUF_SIZE 256
#define PIN_CLK 18
#define PIN_DIN 16

/*
 * basically you just gotta pass in a pointer to the initialize function
 * to tell it where to shove all the encoded voice data.
 * 
 */

i16 mic_buffer[1024];
i16 buf2[1024];

u32 num_speech_samples;
u32 num_modem_samples;

i16 *speech_in;
i16 *modem_out;

// this is gonna be the final output buffer we pull from in the LTE radio
i16 *codec_buffer_out;

// FreeDV / Codec2 expects s16s so this works.

const struct freedv *freedv;

bool flag = 0;
bool WriteFlag = 0;
bool InitializedFlag = 0;
const nrfx_pdm_config_t config;

// rather than fwrite'ing to a block device we want to get a handle or pointer
// to a kernel ring buffer and slap PCM data in for Codec2 then eventually ARM
// hw-accelerated encryption 

static void drv_pdm_cb( const nrfx_pdm_evt_t *event ) {
  if ( event->buffer_requested ) {
    if ( !flag ) {
      i32 error = nrfx_pdm_buffer_set(&mic_buffer[0], 1024);
      flag = 1;
      WriteFlag = 1;
    } else {
      i32 error = nrfx_pdm_buffer_set(&buf2[0], 1024);
      flag = 0;
      WriteFlag = 1;
    }
  }
}

int init_pdm( i16 *output_buffer ) {
  u32 error = 0;

  config = NRFX_PDM_DEFAULT_CONFIG(PIN_CLK, PIN_DIN);
  err = nrfx_pdm_init(&config, drv_pdm_cb);
  
  //err = nrfx_pdm_start();  NO

  err = init_freedv( output_buffer );

  return err;
}

int init_freedv( i16 *output_buffer ) {

  u32 error = 0;

  freedv = freedv_open(FREEDV_MODE_700D);
  if ( freedv == NULL ) {
    err = 1;
    return err;
  }

  num_speech_samples = freedv_get_n_max_speech_samples(freedv);
  num_modem_samples = freedv_get_n_max_modem_samples(freedv);

  speech_in = realloc( speech_in, sizeof(short) * num_speech_samples );
  modem_out = realloc( modem_out , sizeof(short) * num_modem_samples );

  codec_buffer_out = output_buffer;

  InitializedFlag = true;
}

bool is_initialized( void ) {

  if ( InitializedFlag == 1 ) {
    return true;
  } else {
    return false;
  }
}

int listen ( void ) {
  nrfx_pdm_start();
  // event handler takes it from here when the buf is full
}

int stop_listening( void ) {
  nrfx_pdm_stop();
}

// real 200 IQ api design here

int encode_and_write( void ) {

  while ( fread( speech_in, sizeof(i16), num_speech_samples, mic_buffer) == num_speech_samples ) {
    freedv_tx(freedv, modem_out, speech_in );
    fwrite(modem_out, sizeof(i16), num_modem_samples, codec_buffer_out);
  }
}


void nrfx_pdm_event_handler( nrfx_pdm_evt_t *event ) {
  if ( event->buffer_requested ) {
    nrfx_pdm_buffer_set(pdm_buf, PDM_BUF_SIZE);
    int err = encode_and_write();
    if ( err != 0 ) {
      printf("error in freedv_tx, %d", err);
    }
  } 
  if ( event->buffer_released != 0 ) {
    
  }
}
