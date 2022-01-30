#include <nrf9160.h>
#include <zephyr.h>
#include <sys/printk.h>
#include <drivers/i2s.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_SAMPLE_FREQ (44100)
#define AUDIO_SAMPLES_PER_CH_PER_FRAME (128)
#define AUDIO_NUM_CHANNELS (2)
#define AUDIO_SAMPLES_PER_FRAME                                                \
	(AUDIO_SAMPLES_PER_CH_PER_FRAME * AUDIO_NUM_CHANNELS)
#define AUDIO_SAMPLE_BYTES (3)
#define AUDIO_SAMPLE_BIT_WIDTH (24)

#define AUDIO_FRAME_BUF_BYTES (AUDIO_SAMPLES_PER_FRAME * AUDIO_SAMPLE_BYTES)

#define I2S_PLAY_BUF_COUNT (500)

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

int event_handler ( struct hw_evt *event ) {

}
