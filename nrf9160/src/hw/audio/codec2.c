#include <nrf9160.h>
#include <zephyr.h>
#include <sys/printk.h>
#include <drivers/i2s.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>     // mutex


#include "freedv_api.h"

// @TODO lol dummy values.  CHANGE THESE
#define CODEC_THREAD_STACK_SIZE 24000   
#define CODEC_PRIORITY 2


#define AUDIO_SAMPLE_FREQ (44100)
#define AUDIO_SAMPLES_PER_CH_PER_FRAME (128)
#define AUDIO_NUM_CHANNELS (2)
#define AUDIO_SAMPLES_PER_FRAME                                                \
	(AUDIO_SAMPLES_PER_CH_PER_FRAME * AUDIO_NUM_CHANNELS)
#define AUDIO_SAMPLE_BYTES (3)
#define AUDIO_SAMPLE_BIT_WIDTH (24)

#define AUDIO_FRAME_BUF_BYTES (AUDIO_SAMPLES_PER_FRAME * AUDIO_SAMPLE_BYTES)

#define I2S_PLAY_BUF_COUNT (500)


static const k_tid_t codec_thread;

const k_mutex incoming_mutex;
const k_mutex outgoing_mutex;

K_DEFINE_MUTEX( incoming_mutex );
K_DEFINE_MUTEX( outgoing_mutex );

// we should only need to receive HW and RADIO events
struct hw_msg {
  union {
    struct hw_event hw;
    struct radio_event radio;
  } module;
};

u32 num_tx_samples;
u32 num_rx_samples;
u32 num_modem_samples;
// for DECODE / RX side only
size_t num_input_samples, num_output_samples;
i16 *speech_incoming;
i16 *speech_outgoing;
i16 *codec_incoming;
i16 *codec_outgoing;

const struct freedv *freedv;

bool InitializedFlag = 0;

// ??? lol
#define OUTGOING_BUFFER_SIZE 24000
#define SPEAKER_BUFFER_SIZE  I2S_PLAY_BUF_COUNT

i16 *outgoing_message_buffer;
size_t outgoing_message_size;
bool outgoing_buffer_available;
// this is what's gonna be filled with PCM and be pushed to the speaker GPIO or whatever.
i16 *speaker_buffer;

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

// only receiving event messages ( enum / int ) 
K_MSGQ_DEFINE(i2s_msgq, sizeof(int), 10, 4); 

static struct module_data self = {
  .name = "i2s",
  .msg_q = NULL,
  .supports_shutdown = false
};

int init ( void ) {
  int err;

  incoming_message_buffer = NULL;
  incoming_message_size = 0;
  outgoing_buffer_available = true;

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

  init_freedv(outgoing_message_buffer);

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

int start_listening( void ) {
  int err;
  // "receiving" from the microphone 
  err = i2s_trigger( rx_device, I2S_DIR_RX, I2S_TRIGGER_START );
  if ( err !== 0 ) {
    LOG_ERR( "RX: i2s trigger start error: %d.", err );
    return err;
  }
  // this is probably the wrong check.
  outgoing_buffer_available = false;
  while ( i2s_buf_read( rx_device, speech_incoming, AUDIO_FRAME_BUF_BYTES ) == 0 ) {
    encode_and_send();
  }

  SEND_EVENT( hw, HW_EVENT_I2S_TRANSMIT_READY ); 
  return err;
}

int stop_listening( void ) {
  int err;
  err = i2s_trigger( rx_device, I2S_DIR_RX, I2S_TRIGGER_STOP );
  if ( err !== 0 ) {
    LOG_ERR( "RX: i2s trigger stop error: %d.", err );
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
}

int tx_speaker ( size_t size ) {
  SEND_EVENT( hw, HW_EVENT_I2S_PLAYING );
  i2s_buf_write( tx_device, speaker_buffer, size ); 
}

int stop_tx( void ) {
  int err;
  err = i2s_trigger( tx_device, I2S_DIR_TX, I2S_TRIGGER_DRAIN );
  if ( err !== 0 ) {
    LOG_ERROR("TX: i2s trigger drain error: %d.", err);
  }
  SEND_EVENT( hw, HW_EVENT_I2S_DONE_PLAYING );
}

int event_handler ( struct hw_msg *msg ) {
  if ( IS_EVENT( msg, hw, HW_EVENT_PTT_DOWN ) ) { 
    start_listening();
  }
  if ( IS_EVENT( msg, hw, HW_EVENT_PTT_UP ) ) {
    stop_listening();
  }

  if ( IS_EVENT( msg, radio, RADIO_EVENT_INCOMING_MSG_DONE ) ) {
    incoming_message_buffer = msg->module.buffer.ptr;
    incoming_message_size   = msg->module.buffer.len;
    decode_and_play();
  }
}

/*
 * CODEC
 */


int init_freedv( ) {

  u32 error = 0;

  outgoing_message_buffer = malloc( OUTGOING_BUFFER_SIZE );
  speaker_buffer  = malloc( SPEAKER_BUFFER_SIZE  );

  freedv = freedv_open(FREEDV_MODE_700D);
  if ( freedv == NULL ) {
    err = 1;
    return err;
  }

  num_tx_samples = freedv_get_n_max_speech_samples(freedv);
  num_rx_samples = freedv_get_n_max_speech_samples(freedv);
  num_codec_samples = freedv_get_n_max_modem_samples(freedv);

  // there is no way to name these that's not confusing.
  // "outgoing" speech is coming in from the microphone going "out" to the server.
  // "incoming" speech is from the mqtt client going "in" to the speaker.
  
  speech_outgoing = realloc( speech_in, sizeof(short) * num_tx_samples );
  speech_incoming = realloc( speech_out , sizeof(short) * num_rx_samples);
  codec_outgoing  = realloc( codec_out, sizeof(short) * num_codec_samples);
  codec_incoming  = realloc( codec_in,  sizeof(short) * num_codec_samples );

  //outgoing_message_buffer

  InitializedFlag = true;

}

int encode_and_send( void ) {
  int offset = 0;
  size_t len = num_tx_samples;
  // MUTEX
  k_mutex_lock( &outgoing_mutex );

  while ( fread( speech_outgoing, sizeof(i16), num_tx_samples, mic_buffer) == num_tx_samples ) {
    freedv_tx( freedv, codec_outgoing, speech_outgoing );
    fwrite( codec_outgoing, sizeof(i16), num_codec_samples, outgoing_message_buffer );
    outgoing_message_size += num_codec_samples;
  }

  k_mutex_unlock( &outgoing_mutex );
}

int decode_and_play( void ) {
  size_t offset = 0;
  // MUTEX
  k_mutex_lock( &incoming_mutex );
  // 
  while ( offset < incoming_message_size ) {
    offset += fread( codec_incoming, sizeof(i16), num_rx_samples, incoming_message_buffer );
    num_output_samples = freedv_rx(freedv, speech_incoming, codec_incoming);
    num_input_samples = freedv_nin(freedv);
    // last argument is where we put decoded PCM.
    fwrite(speech_incoming, sizeof(short), num_output_samples, speaker_buffer);
    tx_speaker( num_output_samples );
  }
  i2s_trigger( tx_device, I2S_DIR_TX, I2S_TRIGGER_DRAIN );
  k_mutex_unlock( &incoming_mutex );
}

static void i2s_thread_fn( void ) {
  int err;
  int event;

  while ( true ) {
    k_msgq_get( &i2s_msgq, &event, K_FOREVER);

    event_handler( event ); 
  }
}

K_THREAD_DEFINE( codec_thread, CODEC_THREAD_STACK_SIZE,
                 i2s_thread_fn, NULL, NULL, NULL,
                 CODEC_PRIORITY, K_FP_REGS, 0 );
