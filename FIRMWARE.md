# firmware structure

microphone interface: 

https://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.nrf52832.ps.v1.1%2Fpdm.html

## transmit

MQTT session is set up on NRF9160 over TLS.

Microphone -> TLV320ADC or similar -> NRF9160 I2S input.

TLV320 can filter up the signal real nice to give to Codec2.

NRF9160's FPU: Codec2/FreeDV encode payload from voice data.
 
this payload could be a stack of smaller payloads, or just one big payload.

at this point, encrypt with CryptoCell or whatever?  but don't, at first..

voice payload is TX'd to MQTT broker.

## receive

decrypt

Codec2 / FreeDV decode

i2s/c out -> amp -> speaker
