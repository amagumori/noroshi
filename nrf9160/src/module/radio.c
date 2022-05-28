#include <zephyr.h>
#include <stdio.h>
#include <modem/lte_lc.h>
#include <net/socket.h>

#include "modem.h"

// http://wiki.shoutcast.com/wiki/SHOUTcast_2_(Ultravox_2.1)_Protocol_Details


static int client_fd;
static struct sockaddr_storage host_addr;

#define RX_SIZE sizeof(struct m_audio_frame_t)
static char RX_BUFFER[RX_SIZE];   // frame size

struct k_msgq *radio_station_queue;

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

void radio_job( void ) {
  // setup connection

  int err;
  while ( 1 ) {
    err = recv( socket, RX_BUFFER, sizeof(RX_BUFFER) - 1, 0 );
    // check err
    if ( k_msgq_put( &radio_station_queue, rx_buffer, K_NO_WAIT ) != 0 ) {

    }
  }
}
