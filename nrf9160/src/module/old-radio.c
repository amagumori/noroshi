
int transmit( void ) {
  // MUTEX
  k_mutex_lock( &outgoing_mutex );

  int count = outgoing_message_size / CHUNKED_MESSAGE_SIZE;
  
  // @TODO this drops the last chunk in the buffer bc of division behavior.
  u16 *ptr = outgoing_message_buffer;
  for ( int i=0; i < count; i++ ) {
    publish( client, ptr, CHUNKED_MESSAGE_SIZE);
    ptr += CHUNKED_MESSAGE_SIZE;
  }

  k_mutex_unlock( &outgoing_mutex );
  return 1;
}

/* 
 * having sequenced transmissions is a whole other can of worms
 * that can be attempted later.  not minimum viable thing.
int read_payload ( struct mqtt_client *client, struct mqtt_publish_message *msg ) {
  u16 seq = msg->payload.seq;
  u16 offset = number * CONFIG_MQTT_PAYLOAD_BUFFER_SIZE;
  // uh oh
  memcpy(&output_buffer + offset, msg->payload.data, msg->payload.len); 
  message_chunks_remaining--;
  // this is absolutely not the way
}
*/

void switch_buffers( void ) {
  if ( audio_ptr >= rx_buffer_front &&
       audio_ptr <= rx_buffer_front + audio_buffer_offset ) {
    memset( rx_buffer_back, 0, MAX_MESSAGE_SIZE );
    audio_ptr = rx_buffer_back;
    audio_buffer_offset = 0;
    front = false;
  }
  if ( audio_ptr >= rx_buffer_back &&
       audio_ptr <= rx_buffer_back + audio_buffer_offset ) {
    memset( rx_buffer_front, 0, MAX_MESSAGE_SIZE );
    audio_ptr = rx_buffer_front;
    audio_buffer_offset = 0;
    front = true;
  }
  else {
    LOG_ERR("audio_ptr out of bounds. %x", audio_ptr);
    LOG_ERR("front buffer: %x to %x", rx_buffer_front, rx_buffer_front + audio_buffer_offset);
    LOG_ERR("back buffer: %x to %x", rx_buffer_back, rx_buffer_back + audio_buffer_offset);
  }
}

// so this is just throwing our things into a circular buffer
// which i think is the way
int push_audio_payload ( struct mqtt_client *client, struct mqtt_publish_message *msg ) {
  
  k_mutex_lock( &incoming_mutex );

  if ( audio_buffer_offset + msg->payload.len >= MAX_MESSAGE_SIZE ) {
    struct radio_event *evt = new_radio_event();
    evt->type = RADIO_EVENT_INCOMING_MSG_DONE;
    incoming_message_buffer = audio_ptr;
    incoming_message_size   = audio_buffer_offset;

    //evt->buffer_data.ptr = audio_ptr;
    //evt->buffer_data.len = audio_buffer_offset;

    switch_buffers();
    EVENT_SUBMIT( evt );
  }

  memcpy( &audio_ptr + audio_buffer_offset, msg->payload.data, msg->payload.len );
  audio_buffer_offset += msg->payload.len;

  if ( msg->payload.end == true ) {
    struct radio_event *evt = new_radio_event();
    evt->type = RADIO_EVENT_INCOMING_MSG_DONE;
    incoming_message_buffer = audio_ptr;
    incoming_message_size   = audio_buffer_offset;
    switch_buffers();
    EVENT_SUBMIT( evt );
  }

  k_mutex_unlock( &incoming_mutex );

  return 1;
}

