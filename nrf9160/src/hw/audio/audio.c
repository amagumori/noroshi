#include <logging/log.h>
LOG_MODULE_REGISTER(audio, LOG_LEVEL_DBG);

#include <zephyr.h>
#include <stdio.h>
#include <sys/types.h>

#include <linker/sections.h>
#include <errno.h>

#include "audio_server.h"
#include "audio_codec_opus_api.h"
#include "audio_codec_I2S_api.h"

#define OPUS_PRIORITY 10

#define INPUT_DATA_SIZE CONFIG_AUDIO_FRAME_SIZE_SAMPLES*CONFIG_CHANNELS*sizeof(int16_t)
#define OUTPUT_DATA_SIZE CONFIG_AUDIO_FRAME_SIZE_SAMPLES*CONFIG_CHANNELS*sizeof(int16_t)

#define OPUS_ENCODE_STACK_SIZE 20000
#define OPUS_DECODE_STACK_SIZE 50000


K_THREAD_STACK_DEFINE(audio_rx_stack_area, OPUS_ENCODE_STACK_SIZE);
struct k_thread audio_rx_thread_data;
K_THREAD_STACK_DEFINE(audio_tx_stack_area, OPUS_DECODE_STACK_SIZE);
struct k_thread audio_tx_thread_data;;

k_tid_t audio_rx_tid;
k_tid_t audio_tx_tid;

audio_stats_counter_t audio_stats_counter;

static u8   i2s_line_in_backbuffer[INPUT_DATA_SIZE];

static u8_t i2s_line_in[INPUT_DATA_SIZE];
static u8_t i2s_speaker[OUTPUT_DATA_SIZE];

static m_audio_frame_t frame_OPUS_encode;
static m_audio_frame_t frame_OPUS_decode = {.data_size = 0};


K_MSGQ_DEFINE(msgq_OPUS_ENCODE, sizeof(m_audio_frame_t), 1, 4);
struct k_msgq *p_msgq_OPUS_ENCODE = &msgq_OPUS_ENCODE;

K_MSGQ_DEFINE(msgq_OPUS_DECODE, sizeof(m_audio_frame_t), 3, 4);
struct k_msgq *p_msgq_OPUS_DECODE= &msgq_OPUS_DECODE;

K_MSGQ_DEFINE(msgq_i2s_in, INPUT_DATA_SIZE/I2S_BUFFER_RATIO, I2S_BUFFER_RATIO, 4);
struct k_msgq *p_msgq_i2s_in = &msgq_i2s_in;
K_MSGQ_DEFINE(msgq_i2s_out, OUTPUT_DATA_SIZE/I2S_BUFFER_RATIO, I2S_BUFFER_RATIO*2, 4);
struct k_msgq *p_msgq_i2s_out = &msgq_i2s_out;

#ifndef CONFIG_AUDIO_CODEC_SGTL5000

static struct k_sem rx_lock;
static void rx_unlock(struct k_timer *timer_id);
K_TIMER_DEFINE(rx_timer, rx_unlock, NULL);

static void rx_unlock(struct k_timer *timer_id)
{
	k_sem_give(&rx_lock);
}
#endif

void audio_rx_process()
{
	m_audio_frame_t *p_frame = (m_audio_frame_t *) &frame_OPUS_encode;

#if CONFIG_AUDIO_CODEC_SGTL5000
	for(int i=0; i<INPUT_DATA_SIZE; i+= INPUT_DATA_SIZE/I2S_BUFFER_RATIO){
		k_msgq_get(p_msgq_i2s_in, &i2s_line_in[i], K_FOREVER);
	}
#else
	k_sem_take(&rx_lock, K_FOREVER);
#endif

	//
	audio_stats_counter.Opus_enc++;
	/*encode pcm to opus*/
	drv_audio_codec_encode((int16_t *) i2s_line_in, p_frame);
	k_msgq_put(p_msgq_OPUS_ENCODE, p_frame, K_FOREVER);
}

void audio_tx_process()
{
	m_audio_frame_t *p_frame = &frame_OPUS_decode;

	if(k_msgq_get(p_msgq_OPUS_DECODE, p_frame, K_NO_WAIT) == 0) {
		/*decode opus to pcm*/
		drv_audio_codec_decode(p_frame, (int16_t *)i2s_speaker);
    	audio_stats_counter.Opus_dec++;
	}else{
		//LOG_ERR("Missing frame");
		p_frame->data_size = 0;
		//
		drv_audio_codec_decode(p_frame, (int16_t *)i2s_speaker);
    	audio_stats_counter.Opus_gen++;
	}

	for(int i=0; i<OUTPUT_DATA_SIZE; i+= OUTPUT_DATA_SIZE/I2S_BUFFER_RATIO){
		k_msgq_put(p_msgq_tx_out, &i2s_speaker[i], K_FOREVER);
	}
}

static void audio_rx_task(){

	LOG_INF("start Audio_Rx_Task" );
#ifndef CONFIG_AUDIO_CODEC_SGTL5000
	k_sem_init(&rx_lock, 0, UINT_MAX);
	k_timer_start(&rx_timer, CONFIG_AUDIO_FRAME_SIZE_MS, CONFIG_AUDIO_FRAME_SIZE_MS);
#endif
	while(1){
		audio_rx_process();
	}
}

static void audio_tx_task(){

	LOG_INF("start Audio_Tx_Task" );
	while(1){
		audio_tx_process();
	}
}

void audio_system_init()
{
	audio_codec_opus_init();

#if CONFIG_AUDIO_CODEC_SGTL5000
	audio_codec_I2S_init(p_msgq_i2s_in, p_msgq_i2s_out);
#endif

	Audio_Rx_tid = k_thread_create(&audio_rx_thread_data, audio_rx_stack_area,
	                                 K_THREAD_STACK_SIZEOF(audio_rx_stack_area),
									 audio_rx_task,
	                                 NULL, NULL, NULL,
									 OPUS_PRIORITY, 0, K_NO_WAIT);

	Audio_Tx_tid = k_thread_create(&audio_tx_thread_data, audio_tx_stack_area,
	                                 K_THREAD_STACK_SIZEOF(audio_tx_stack_area),
									 audio_tx_task,
	                                 NULL, NULL, NULL,
									 OPUS_PRIORITY, 0, K_NO_WAIT);

	k_sleep(K_MSEC(1000));
#if CONFIG_AUDIO_CODEC_SGTL5000
	LOG_INF("Audio_start");
	audio_codec_I2S_start();
#endif
}

void audio_system_stop(){


}



