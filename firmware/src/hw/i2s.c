#include <nrf9160.h>
#include <zephyr.h>
#include <sys/printk.h>
#include <drivers/i2s.h>
#include <stdlib.h>
#include <string.h>

#include "freedv_api.h"

#define AUDIO_SAMPLE_FREQ (44100)
#define AUDIO_SAMPLES_PER_CH_PER_FRAME (128)
#define AUDIO_NUM_CHANNELS (2)
#define AUDIO_SAMPLES_PER_FRAME                                                \
	(AUDIO_SAMPLES_PER_CH_PER_FRAME * AUDIO_NUM_CHANNELS)
#define AUDIO_SAMPLE_BYTES (3)
#define AUDIO_SAMPLE_BIT_WIDTH (24)

#define AUDIO_FRAME_BUF_BYTES (AUDIO_SAMPLES_PER_FRAME * AUDIO_SAMPLE_BYTES)

#define I2S_PLAY_BUF_COUNT (500)

struct hw_msg {
  union {
    struct hw_event hw;
    struct ui_event ui;
    struct modem_event modem;
    struct radio_event radio;
    struct server_event server;
  } module;
};

u32 num_tx_samples;
u32 num_rx_samples;
u32 num_modem_samples;

// for DECODE / RX side only
size_t num_input_samples, num_output_samples;

i16 *speech_in;
i16 *speech_out;
i16 *modem_out;
i16 *modem_in;

const struct freedv *freedv;

bool flag = 0;
bool WriteFlag = 0;
bool InitializedFlag = 0;
const nrfx_pdm_config_t config;

// this is gonna be the final output buffer we pull from in the LTE radio
i16 *encode_buffer_out;
// this is what's gonna be filled with PCM and be pushed to the speaker GPIO or whatever.
i16 *decode_buffer_out;

// this will point directly at the stream coming off the air 
u16 *incoming_message_buffer;
size_t incoming_message_size;

static struct i2s_config rx_config;
static struct i2s_config tx_config;
static const struct device *rx_device;
static const struct device *tx_device;

static struct k_mem_slab rx_mem_slab;
static struct k_mem_slab tx_mem_slab;
static char rx_buffer[AUDIO_FRAME_BUF_BYTES * I2S_PLAY_BUF_COUNT];
static char tx_buffer[AUDIO_FRAME_BUF_BYTES * I2S_PLAY_BUF_COUNT];
static int ret;

int init ( void ) {
  int err;

  incoming_message_buffer = NULL;
  incoming_message_size = 0;

  rx_device = device_get_binding("i2s_rx");
  if (!rx_device) {
    printk("unable to find i2s_rx device\n");
    exit(-1);
  }
  tx_device = device_get_binding("i2s_tx");
  if (!tx_device) {
    printk("unable to find i2s_tx device\n");
    exit(-1);
  }
  k_mem_slab_init(&rx_mem_slab, rx_buffer, AUDIO_FRAME_BUF_BYTES, I2S_PLAY_BUF_COUNT);
  k_mem_slab_init(&tx_mem_slab, tx_buffer, AUDIO_FRAME_BUF_BYTES, I2S_PLAY_BUF_COUNT);

  err = configure();
  if ( err != 0 ) {
    printk("i2s configuration func failed...\n");
    exit(-1);
  }

  return 1;
}

int cleanup ( char *buffer ) {
  //
}

int configure( void ) {
  int err;

  rx_config.word_size = AUDIO_SAMPLE_BIT_WIDTH;
  rx_config.channels = AUDIO_NUM_CHANNELS;
  rx_config.format = I2S_FMT_DATA_FORMAT_I2S;
  rx_config.options = I2S_OPT_FRAME_CLK_SLAVE;
  rx_config.frame_clk_freq = AUDIO_SAMPLE_FREQ;
  rx_config.mem_slab = &rx_mem_slab;
  rx_config.block_size = AUDIO_FRAME_BUF_BYTES;
  rx_config.timeout = -1;

  err = i2s_configure(rx_device, I2S_DIR_RX, &rx_config);

  if ( err != 0 ) {
    printk("i2s config failed with error %d", err);
    exit(-1);
  }

  tx_config.word_size = AUDIO_SAMPLE_BIT_WIDTH;
  tx_config.channels = AUDIO_NUM_CHANNELS;
  tx_config.format = I2S_FMT_DATA_FORMAT_I2S;
  tx_config.options = I2S_OPT_FRAME_CLK_SLAVE;
  tx_config.frame_clk_freq = AUDIO_SAMPLE_FREQ;
  tx_config.block_size = AUDIO_FRAME_BUF_BYTES;
  tx_config.mem_slab = &tx_mem_slab;
  tx_config.timeout = -1;
  
  err = i2s_configure(tx_device, I2S_DIR_TX, &tx_config);

  if ( err != 0 ) {
    printk("i2s config failed with error %d", err);
    exit(-1);
  }
}

// we RX from the microphone and TX to the speaker
// even though we "TX" the mic data and "RX" the incoming messages.
// LOL

int start_rx( void ) {
  int err;
  // "receiving" from the microphone 
  err = i2s_trigger( rx_device, I2S_DIR_RX, I2S_TRIGGER_START );
  if ( err !== 0 ) {
    LOG_ERR( "RX: i2s trigger start error: %d.", err );
  }
  return err;
}

int stop_rx( void ) {
  int err;
  err = i2s_trigger( rx_device, I2S_DIR_RX, I2S_TRIGGER_DRAIN );
  if ( err !== 0 ) {
    LOG_ERR( "RX: i2s trigger drain error: %d.", err );
  }
  return err;
}

int start_tx( void ) {
  size_t size;
  void *rx_mem_block, *tx_mem_block;
  int err;
  err = i2s_trigger( tx_device, I2S_DIR_TX, I2S_TRIGGER_START );
  if ( err !== 0 ) {
    LOG_ERR("TX: i2s trigger start error: %d.", err );
    return err;
  }
  i2s_buf_write( tx_device, incoming_message_buffer, incoming_message_size);
}

int stop_tx( void ) {

}

int event_handler ( struct hw_msg *msg ) {
  if ( IS_EVENT( msg, hw, HW_EVENT_PTT_DOWN ) ) { 
    start_rx();
  }
  if ( IS_EVENT( msg, hw, HW_EVENT_PTT_UP ) ) {
    stop_rx();
  }

  if ( IS_EVENT( msg, radio, RADIO_EVENT_INCOMING_MSG_DONE ) ) {
    incoming_message_buffer = msg->module.buffer.ptr;
    incoming_message_size   = msg->module.buffer.len;
    encode_and_write();
    start_tx();
  }
}

/*
 * CODEC
 */


int init_freedv( i16 *output_buffer ) {

  u32 error = 0;

  freedv = freedv_open(FREEDV_MODE_700D);
  if ( freedv == NULL ) {
    err = 1;
    return err;
  }

  num_tx_samples = freedv_get_n_max_speech_samples(freedv);
  num_rx_samples = freedv_get_n_max_speech_samples(freedv);
  num_modem_samples = freedv_get_n_max_modem_samples(freedv);

  speech_in = realloc( speech_in, sizeof(short) * num_tx_samples );
  speech_out = realloc( speech_out , sizeof(short) * num_rx_samples);
  // same buffer size for tx and rx for now 
  modem_out = realloc( modem_out , sizeof(short) * num_modem_samples );
  modem_in = realloc( modem_in , sizeof(short) * num_modem_samples );

  encode_buffer_out = output_buffer;

  InitializedFlag = true;

}

int encode_and_write( void ) {

  while ( fread( speech_in, sizeof(i16), num_tx_samples, mic_buffer) == num_tx_samples ) {
    freedv_tx(freedv, modem_out, speech_in );
    fwrite(modem_out, sizeof(i16), num_modem_samples, encode_buffer_out);
  }
}

// drive speaker with I2S; use EasyDMA with two buffers - a "playing" buffer and a 
// "preparing" buffer, basically classic double buffering.
// we're going to throw PCM blocks straight at that buffer from this function.

int decode_and_write( void ) {
  while ( fread(demod_in, sizeof(i16), num_rx_samples, recv_buffer ) == num_rx_samples ) {
    num_output_samples = freedv_rx(freedv, speech_out, modem_in);
    num_input_samples = freedv_nin(freedv);
    fwrite(speech_out, sizeof(short), num_output_samples, decode_buffer_out);
  }
}


