#include <stdio.h>
#include <string.h>
// ESP32 doesn't even have stat probably
#include <sys/stat.h>
// it's in newlib
#include <ftw.h>
//
#include "freertos/queue.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

/* INTEGRATING RDIFF FOR FILE DIFFING AND UPDATING
 *
 * SIGNATURE - rs_sig_file ( whole.c : 66 )
 * DELTA - 
 *  rs_loadsig_file (whole.c : 89)
 *    FILE*,
 *    rs_signature_t ** sum_set
 *    rs_stats_t *
 *  rs_delta_file   (whole.c : 107)
 *    rs_signature_t *
 *    FILE *
 *    FILE *
 *    rs_stats_t *
 *  rs_patch_file   (whole.c : 123)
 *    FILE *
 *    FILE *
 *    FILE *
 *    rs_stats_t *
 *
 */

/*
 * use ESP-NOW for connection negotiation and syncing.
 * first - key exchange and auth
 * then - get signatures
 * generate deltas
 * send deltas
*/

enum msg_type_t {
  MSG_REQ_FILE_INFO,
  MSG_DONE_SENDING_FILE_INFO,
  MSG_REQ_SIGS,
  MSG_READY_RECV_SIGS,
  MSG_READY_SEND_SIGS,
  MSG_READY_RECV_DELTAS,
  MSG_READY_SEND_DELTAS
} msg_type;

// simple "tagged union" packet format.
typedef struct packet_t {
  enum msg_type_t type;
  union {
    struct modified_t file_modified;
  } data;
} packet;

// internal state
enum process_state_t {
  SYNC_IDLE,
  SYNC_RECEIVING_INFO,
  SYNC_RECEIVING_SIGS,
  SYNC_RECEIVING_DELTAS,
  SYNC_COMPLETE,
  SYNC_ERROR
} internal_state;

// external state sent to NRF9160 for UI
enum sync_state_t {
  SYNC_STATE_DISCONNECTED,
  SYNC_STATE_CONNECTED,
  SYNC_STATE_FILE_INFO_START,
  SYNC_STATE_FILE_INFO_END,
  SYNC_STATE_SIG_TRANSFER_START,
  SYNC_STATE_SIG_TRANSFER_END,
  SYNC_STATE_GENERATING_DELTAS,
  SYNC_STATE_PATCHING,
  SYNC_STATE_ERROR
} external_state;

QueueHandle_t sync_state_queue;
TaskHandle_t Sync_Task;
FILE *socket;

void task ( void *pvParam ) {
  int len;
  char rx_buffer[128];

  do {
    len = recv(socket, rx_buffer, sizeof(rx_buffer) - 1, 0);
    if ( len < 0 ) {
      ESP_LOGE(TAG, "error in socket receiving: errno %d", errno);
    } else if ( len == 0 ) {
      ESP_LOGW(TAG, "connection closed.");
    } else {
      rx_buffer[len] = 0;
      ESP_LOGI(TAG, "received %d bytes: %s", len, rx_buffer);

      struct packet_t packet = rx_buffer;

      switch ( packet.type ) {

      }

      switch ( internal_state ) {
        case SYNC_IDLE:
          break;
        case SYNC_SEND_INFO: 
          walk_file_info("/");

          break;
        case SYNC_RECV_INFO:
          break;
        case SYNC_SEND_SIGS:
          break;
        case SYNC_RECV_SIGS:
          break;
        case SYNC_SEND_DELTAS:
          break;
        case SYNC_RECV_DELTAS:
          break;
        case SYNC_ERROR:
          break;
        default: 
          break;
      }
    }
  } while ( len > 0 );

}

// pass in socket fd here.
int init ( int fd ) {
  socket = fopen(fd);
  sync_state_queue = xQueueCreate(5, sizeof(sync_state_t));
  // taskfunc, name, stacksize, param, priority, taskhandle
  xTaskCreate(task, "sync_task", 2048, pvParam, 10, NULL);
}

void shutdown( void ) {
  fclose(socket);
}

int send( void *payload, size_t len ) {
  if ( sockopt(socket, SOL_SOCKET, SO_ERROR, &err, &len) != 0 ) {
    ESP_LOGE(TAG, "socket error in signature generation.");
    return -1;
  }

  fwrite(payload, len, 1, socket);
  return 1;
}

// we can assume directory structure will match starting at $BASEPATH for syncing.

// just hardcoding in BLAKE2 hashing
const rs_magic_number sig_magic = RS_BLAKE2_SIG_MAGIC;

// dumbest time comparison function ever to start with.
// a newer: positive
// b newer: negative
// same seconds: 0
int time_newer( struct timespec *a, struct timespec *b ) {
  if ( a->tv_sec > b->tv_sec ) {
    return 1;
  } else if ( b->tv_sec > a->tv_sec ) {
    return -1;
  } else {
    return 0;
  }
}

typedef struct modified {
  const char path[PATH_MAX];
  const struct timespec modified;
} modified_t;

int send_info( const char *path, const struct stat *stat, int flag ) {
  struct modified_t mod = {0};
  // @TODO BOUNDS CHECK
  sprintf(mod.path, "%s", path);
  mod.modified = stat.st_mtime;
  send( mod );
  return 1;
}

int walk_file_info( char *path ) {
  ftw( path, send_info, 50 );
}

int check_info( struct modified_t *mod ) {
  struct stat s;
  if ( stat(mod.path, &s) == -1 ) {
    return 0;
    // actually do nothing because it'll be put in the peer's to-send queue anyway
  }
  int compare = time_newer( s.st_mtime, mod.modified );
  if ( compare > 0 ) {
    send_sig( mod.path );
    return 1;
  } else {
    return 0;
  }
}

// passing in socket pointer with fdopen(), to send signature directly ota
rs_result generate_signature( const char *path, FILE *sock ) {
  FILE *basis;
  //  FILE *sig;
  rs_stats_t stats;
  rs_result result;
  // way more to it than this, handle 
  //const char *sig_path = strcat( filepath, ".sig" );

  basis = rs_file_open(path, "rb");
  //sig = rs_file_open(sig_path, "wb");

  const int block_size = 0; 
  const int strength = 0; 
  const int strong_len = 0;

  int err = 0;
  socklen_t len = sizeof(err);
  if ( sockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) != 0 ) {
    ESP_LOGE(TAG, "socket error in signature generation.");
  }

  struct packet_t packet = {
    .type = MSG_SIG_INFO,
    .sig_info = {
      .path = path
    }
  };

  send_packet( &packet );

  result = rs_sig_file(basis, sock, block_size, strong_len, sig_magic, &stats); 

  send_state( MSG_SIG_SEND_COMPLETE );

  //  rs_file_close(sig);
  rs_file_close(basis);

  return result;
}

rs_result generate_delta( char *sig_path, char *local_path, char *delta_path ) {

  // @FIXME this is not the way to generate delta filename
  // we'll do this outside: const char delta_path = strcat(sig_path, ".delta");
  //

  FILE *sig_file = rs_file_open(sig_path, "rb");
  FILE *local_file = rs_file_open(local_path, "rb");
  FILE *delta_file = rs_file_open(delta_path, "wb");

  rs_result result;
  rs_signature_t *sumset;
  rs_stats_t stats;

  if ( ( result = rs_build_hash_table(sumset) ) != RS_DONE ) {
    return result;
  }

  // CHECK ARG ORDER
  result = rs_delta_file( sumset, local_file, delta_file, &stats );
  
  rs_file_close(delta_file);
  rs_file_close(local_file);
  rs_file_close(sig_file);

  rs_free_sumset(sumset);

  return result;
}

//rs_result patch( 
