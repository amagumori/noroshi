#include <zephyr.h>
#include <stdio.h>
#include <modem/lte_lc.h>
#include <net/socket.h>

#include "modem.h"

#include "hw/audio.h"

// http://wiki.shoutcast.com/wiki/SHOUTcast_2_(Ultravox_2.1)_Protocol_Details

static int client_fd;
static struct sockaddr_storage host_addr;

#define FRAME_SIZE sizeof(struct m_audio_frame_t)
static char RX_BUFFER[FRAME_SIZE];   
static char TX_BUFFER[FRAME_SIZE];   

K_MSGQ_DEFINE(msgq_station_rx, sizeof(struct m_audio_frame_t), 10, 4);
struct k_msgq *station_rx = &msgq_station_rx;
extern struct k_msgq *p_msgq_OPUS_ENCODE;

// not doing this unless it ends up being needed.
/*
K_MSGQ_DEFINE(msgq_station_tx, sizeof(struct m_audio_frame_t), 10, 4);
struct k_msgq *station_tx = &msgq_station_tx;
*/

// @TODO determine optimum queue size = number of audio frames
#define DATA_QUEUE_ENTRY_COUNT 10
#define DATA_QUEUE_ALIGNMENT 4
K_MSGQ_DEFINE(radio_station_queue, sizeof( struct m_audio_frame_t ), DATA_QUEUE_ENTRY_COUNT, DATA_QUEUE_ALIGNMENT);

// we're gonna put this in its own thread
// and break out all the socket stuff to its own file.

static void server_disconnect(void)
{
	(void)close(client_fd);
}

static int server_init(void)
{
	struct sockaddr_in *server4 = ((struct sockaddr_in *)&host_addr);

	server4->sin_family = AF_INET;
	server4->sin_port = htons(CONFIG_UDP_SERVER_PORT);

	inet_pton(AF_INET, CONFIG_UDP_SERVER_ADDRESS_STATIC,
		  &server4->sin_addr);

	return 0;
}

static int server_connect(void)
{
	int err;

	client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (client_fd < 0) {
		printk("Failed to create UDP socket: %d\n", errno);
		err = -errno;
		goto error;
	}

	err = connect(client_fd, (struct sockaddr *)&host_addr,
		      sizeof(struct sockaddr_in));
	if (err < 0) {
		printk("Connect failed : %d\n", errno);
		goto error;
	}

	return 0;

error:
	server_disconnect();

	return err;
}

// we want to monitor our connection health / bitrate and drop down to a lower-bitrate
// stream if we can't handle X kbps.

void radio_rx_job( void ) {
  // setup connection

  int err;
  while ( 1 ) {
    err = recv( socket, RX_BUFFER, sizeof(RX_BUFFER) - 1, 0 );
    // check err
    if ( k_msgq_put( &station_rx, rx_buffer, K_NO_WAIT ) != 0 ) {
      LOG_ERR("STATION: couldn't put RX frame in message queue.");
    }
  }
}

void radio_tx_job( void ) {
  int err;
  while ( 1 ) {
    if ( k_msgq_get( &p_msgq_OPUS_ENCODE, tx_buffer, K_NO_WAIT ) != 0 ) {
      LOG_ERR("STATION: couldn't get TX frame from TX msgq.");
    } else {
      err = send( socket, TX_BUFFER, sizeof(TX_BUFFER) - 1, 0 );
      if ( err ) {
        LOG_ERR("STATION [TX]: error sending audio frame on socket %d", socket);
      }
    }
  }
}

void thread_fn( void ) {

}

K_THREAD_DEFINE(station_thread, CONFIG_STATION_THREAD_STACK_SIZE,
		thread_fn, NULL, NULL, NULL,
		5, 0, 0);
// figure out a specific priority for this one

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE_EARLY(MODULE, radio_event);
EVENT_SUBSCRIBE(MODULE, hw_event );

