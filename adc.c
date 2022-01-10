#include <nrf9160.h>
#include <drivers/adc.h>
#include <zephyr.h>
#include <stdio.h>

#define ADC_DEVICE_NAME DT_ADC_0_NAME
#define ADC_RESOLUTION 10
#define ADC_GAIN ADC_GAIN_1_6
#define ADC_REFERENCE ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)
#define ADC_1ST_CHANNEL_ID 0
#define ADC_1ST_CHANNEL_INPUT NRF_SAADC_INPUT_AIN0
#define ADC_2ND_CHANNEL_ID 2
#define ADC_2ND_CHANNEL_INPUT NRF_SAADC_INPUT_AIN2

const struct adc_channel_cfg first_channel_cfg = {
  .gain = ADC_GAIN,
  .reference = ADC_REFERENCE,
  .acquisition_time = ADC_ACQUISITION_TIME,
  .channel_id = ADC_1ST_CHANNEL_ID,
#if defined(CONFIG_ADC_CONFIGURABLE_INPUTS)
  .input_positive = ADC_1ST_CHANNEL_INPUT,
#endif
};

int init_adc( void ) {
  int err = true;

  adc_dev = device_get_binding("ADC_0");
  if ( adc_dev == NULL ) {
    printf("device_get_binding ADC_0 failed\n");
    err = false;
  } else {
    err = adc_channel_setup(adc_dev, &first_channel_cfg);
    if ( err ) {
      printf("error setting up adc: %d\n", err);
      err = false;
    } else {
      printf("adc setup is OK\n");
      err = true;
    }
  }
  return err;
}

int sample( void ) {
  int return_code;

  const struct adc_sequence sequence = {
    .channels = BIT(ADC_1ST_CHANNEL_ID),
    .buffer = sample_buffer,
    .buffer_size = sizeof(sample_buffer),
    .resolution = ADC_RESOLUTION
  };

  if ( !adc_dev ) {
    return -1;
  }

  return_code = adc_read(adc_dev, &sequence);
  // adc_result = &sample_buffer[0];
  return return_code;
}
