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

#include "types.h"

/* **************************************************
 *
 * There are two strategies we can approach syncing with.
 * We can either 
 *
 * **************************************************
*/

// i guess...?
#define PATH_MAX 128
#define CHUNK_SIZE 1024

const int SOCKET;
const char file_rx_buffer[1024];

typedef struct modified {
  const char path[PATH_MAX];
  const struct timespec time;
} modified_t;

enum process_state_t {
  SYNC_IDLE,
  SYNC_CONNECTING,
  SYNC_CONNECTED,
  SYNC_RECEIVING_INFO,
  SYNC_RECEIVING_SIGS,
  SYNC_RECEIVING_DELTAS,
  SYNC_COMPLETE,
  SYNC_ERROR
} internal_state;

enum msg_type_t {
  MSG_GIMME_FILE_INFO,
  MSG_FILE_INFO,
  MSG_DONE_SENDING_FILE_INFO,
  MSG_GIMME_SIGS,
  MSG_SIG_INFO,
  MSG_SIG_CHUNK,
  MSG_SIG_DONE,
  MSG_SIG_RECV_COMPLETE,
  MSG_SIG_RECV_ERROR,
  //MSG_
  MSG_GIMME_DELTAS,
  MSG_DELTA,
  MSG_DELTA_COMPLETE
} msg_type;

typedef struct sig_info {
  const char path[PATH_MAX];
  //u32 size;
} sig_info_t;

typedef struct sig_chunk {
  const char data[CHUNK_SIZE];
} sig_chunk_t;

typedef struct delta_info {
  const char path[PATH_MAX];
  u32 size;
} delta_info_t;

typedef struct delta_chunk {
  const char data[CHUNK_SIZE];
} delta_chunk_t;

// simple "tagged union" packet format.
typedef struct packet_t {
  enum msg_type_t type;
  union {
    struct modified_t file_modified;
    struct sig_info_t sig_info;
    struct sig_chunk_t sig_chunk;
    struct delta_info_t delta;
    struct delta_chunk_t delta_chunk;
  } data;
} packet;

typedef struct other_packet_t {
  enum msg_type_t msg;
  const char path[PATH_MAX];
  struct timespec time;
} other_packet;

void task_synchronous( void *pvParam ) {
  int len;
  char rx_buffer[1024];
  char tx_buffer[1024];
  FILE *fp;
  u32   fsize = 0;

  if ( internal_state == SYNC_IDLE ) {
    struct sockaddr_in destination;
    destination.sin_addr.s_addr = inet_addr(host_ip);
    destination.sin_family = AF_INET;
    destination.sin_port = htons(PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    SOCKET = socket(addr_family, SOCK_STREAM, ip_protocol);
    if ( SOCKET < 0 ) {
      ESP_LOGE(TAG, "unable to create socket: errno %d", errno);
      break;
    }
    ESP_LOGI(TAG, "socket created, connecting to %s:%d", host_ip, PORT);
    internal_state = SYNC_CONNECTING;

    int err = connect(SOCKET, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
    if ( err != 0 ) {
      ESP_LOGE(TAG, "unable to connect: errno %d", errno);
      break;
    }
    ESP_LOGI(TAG, "successfully connected!");
    internal_state = SYNC_CONNECTED;

  }

  do {
    len = recv( SOCKET, rx_buffer, sizeof( struct other_packet_t ), 0 );
    if ( len < 0 ) {

    } else if ( len == 0 ) {

    } else {
      rx_buffer[len] = 0;
      ESP_LOGI(TAG, "received %d bytes: %s", len, rx_buffer);
      // how do you correctly cast the data?
      struct other_packet_t *packet = (struct packet_t *)rx_buffer;
      // we "ack" the state packets by sending the same "request" back.
      switch ( packet->msg ) {

        case MSG_FILE:
          char sig_path[PATH_MAX];
          char delta_path[PATH_MAX];
          FILE *fp;
          size_t str_size;

          strncpy( sig_path, packet->path, PATH_MAX - 1 );
          strncpy( delta_path, packet->path, PATH_MAX - 1 );
          str_size = strlcat( sig_path, ".sig", sizeof(sig_path) );
          if ( str_size >= sizeof(sig_path) ) {
            ESP_LOGE(TAG, "error appending .sig.  pathname %i too long.", str_size);
            break;
          }
          strncat( delta_path, ".delta", sizeof(delta_path) );
          if ( str_size >= sizeof(delta_path) ) {
            ESP_LOGE(TAG, "error appending .delta.  pathname %i too long.", str_size);
            break;
          }
          int time_diff = compare_time(packet->time);

          if ( time_diff == 1 ) {
            // we have a newer version and we need to get sig to generate delta. 
            send_msg( MSG_REQ_SIG );
            int sig_len = 0;
            fp = fopen(sig_path, "w+");
            size_t written = 0;
            size_t write_len = 0;

            // RECEIVE SIG

            do {
              sig_len = recv( SOCKET, rx_buffer, CHUNK_SIZE, 0 );
              write_len = fwrite( rx_buffer, sizeof(char), sig_len, fp );
              written += write_len;
            } while ( sig_len > 0 && write_len == CHUNK_SIZE );
            fclose(fp);
            
            // GENERATE AND SEND DELTA 

            struct rs_result res = generate_delta(sig_path, local_path, delta_path);
            // check our rs_result
            
            fp = fopen(delta_path, "r+");
            
            size_t num_bytes = 0;
            size_t sent = 0;
            while ( ( num_bytes = fread(tx_buffer, sizeof(char), CHUNK_SIZE, fp)) > 0 ) {
              int offset = 0;
              while( (sent = send( SOCKET, tx_buffer + offset, num_bytes, 0 )) > 0 ) {
                offset += sent;
                num_bytes -= sent;
              }
            }

            send_state( MSG_DELTA_COMPLETE );

          } else if ( time_diff < 0 ) {
            // we have an older version and we need to send our signature.
            fp = fopen(packet->path, "r+");
            size_t num_bytes = 0;
            size_t sent = 0;
            while ( ( num_bytes = fread(tx_buffer, sizeof(char), CHUNK_SIZE, fp) ) > 0 ) {
              int offset = 0;
              while( (send = send(SOCKET, tx_buffer+offset, num_bytes, 0) ) > 0 ) {
                offset += sent;
                num_bytes -= sent;
              } 
            }
            fclose(fp);
            fp = fopen(delta_path, "w+");

            int delta_len = 0;
            int write_len = 0;
            int written = 0;
            do {
              delta_len = recv( SOCKET, rx_buffer, CHUNK_SIZE, 0 );
              write_len = fwrite( rx_buffer, sizeof(char), sig_len, fp );
              written += write_len;
            } while ( delta_len > 0 && write_len == CHUNK_SIZE );
            fclose(fp);
            
            // process / apply delta

          } else {
            break;
          }

          break;
      }
    }
  } while( len > 0 );


}

void task ( void *pvParam ) {
  int len;
  char rx_buffer[128];
  FILE *fp;
  u32   fsize = 0;

  if ( internal_state == SYNC_IDLE ) {
    struct sockaddr_in destination;
    destination.sin_addr.s_addr = inet_addr(host_ip);
    destination.sin_family = AF_INET;
    destination.sin_port = htons(PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    SOCKET = socket(addr_family, SOCK_STREAM, ip_protocol);
    if ( SOCKET < 0 ) {
      ESP_LOGE(TAG, "unable to create socket: errno %d", errno);
      break;
    }
    ESP_LOGI(TAG, "socket created, connecting to %s:%d", host_ip, PORT);
    internal_state = SYNC_CONNECTING;

    int err = connect(SOCKET, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
    if ( err != 0 ) {
      ESP_LOGE(TAG, "unable to connect: errno %d", errno);
      break;
    }
    ESP_LOGI(TAG, "successfully connected!");
    internal_state = SYNC_CONNECTED;

  }

  do {
    len = recv(socket, rx_buffer, sizeof(rx_buffer) - 1, 0);
    if ( len < 0 ) {
      ESP_LOGE(TAG, "error in socket receiving: errno %d", errno);
    } else if ( len == 0 ) {
      ESP_LOGW(TAG, "connection closed.");
    } else {
      rx_buffer[len] = 0;
      ESP_LOGI(TAG, "received %d bytes: %s", len, rx_buffer);

      // how do you correctly cast the data?
      struct packet_t *packet = (struct packet_t *)rx_buffer;

      // we "ack" the state packets by sending the same "request" back.
      switch ( packet->type ) {

        case MSG_GIMME_FILE_INFO:
          // start with our "ack"
          send_state( MSG_REQ_FILE_INFO );
          send_mtimes( "/", SOCKET );
          break;

        case MSG_FILE_INFO:
          if ( compare_time( packet->info.modified ) == 1 ) {
            // we have a newer version and we need to get sig to generate delta. 
            send_state( MSG_REQ_SIG );
          }
          if ( compare_time( packet->info.modified ) < 0  ) {
            // we have an older versiona nd we need to send our sig.
            struct sig_info i = {

            };
            struct packet_t p = {
              .msg = MSG_SIG_INFO,
              .data = {
                .sig_info = 
              }
            };
            generate_signature( packet->info.path, SOCKET );
          }
          break;
          
        case MSG_SIG_INFO:
          // this is just going to blast straight through and send deltas back.
          //
          // there has to be a better way, or is there?
          // do we have enough ram to build a full files-to-update list?
          char sig_path[PATH_MAX];
          strncpy( sig_path, packet->sig->path, strlen(packet->sig->path) );
          char local_path[PATH_MAX] = strndup( sig_path, strlen(sig_path) );
          char delta_path[PATH_MAX] = strndup( local_path, strlen(local_path) );
          strncat( delta_path, ".delta", 6 );
          strncat( sig_path, ".rsig", 5 );

          fp = fopen(sig_path, "w+");
          
          if ( fp == NULL ) {
            ESP_LOGE(TAG, "error opening file for writing: %s", packet->sig->path);
            break;
          }

          // we go straight into receiving the whole sig file w/out a state change
          do {
            len = recv(socket, rx_buffer, sizeof(rx_buffer) - 1, 0);
            packet = (struct packet_t *)rx_buffer;
            fwrite( packet->sig_chunk->data, sizeof(char), 1024, fp );
            if ( wlen != 1024 ) {

            }
          } while ( len > 0 && packet->info != MSG_SIG_DONE );

          fclose(fp);

          struct rs_result res = generate_delta(sig_path, local_path, delta_path);
          // check our rs_result
          
          fp = fopen(delta_path, "r+");
          
          size_t num_bytes = 0;
          size_t sent = 0;
          while ( ( num_bytes = fread(tx_buffer, sizeof(char), CHUNK_SIZE)) > 0 ) {
            int offset = 0;
            while( (sent = send( SOCKET, tx_buffer + offset, num_bytes, 0 )) > 0 ) {
              offset += sent;
              num_bytes -= sent;
            }
          }

          send_state( MSG_DELTA_COMPLETE );

          //fsize = packet->sig->size;
          break;

        /*
        case MSG_SIG_CHUNK:
          size_t wlen = fwrite(packet->sig_chunk->data, sizeof(char), 1024, fp);
          if ( wlen != 1024 ) {
            ESP_LOGI(TAG, "finished writing sig.");
          }
          if ( fclose(fp) != 0 ) {
            ESP_LOGE(TAG, "error closing file??");  
          }

          break;
        */
        case MSG_SIG_DONE:
          generate_delta( 


        case MSG_DONE_SENDING_FILE_INFO:



        case MSG_GIMME_SIGS:

          case MSG_READY_

      }

    }
  } while ( len > 0 );

}

int compare_time( struct modified_t info ) {
  struct stat s;
  int ret = stat(info.path, &s);
  if ( ret != 0 ) {
    ESP_LOGE(TAG, "error stat-ing file %s.", info.path);
    return -1;
  }

  if ( s.st_mtime.tv_sec > info.time.tv_sec ) {
    return 1;
  } else if ( b->tv_sec > a->tv_sec ) {
    return -1;
  } else {
    return 0;
  }
}


void send_data ( void *data, size_t len, int fd ) {
  /*
   * int sock_state = 0;
  size_t err_len = sizeof(sock_state);
  int ret = getsockopt( fd, SOL_SOCKET, SO_ERROR, &err_len, &error );
  if ( ret != 0 ) {
    return -1;
  } else {
  */
  if ( send( fd, data, len, 0) != -1 ) {
    ESP_LOGE(TAG, "Error in sending dirinfo.");
  }

}

// we have a TX loop, iterating dirs and sending mtimes
// RX loop:
// receive a dir_modified_t
// stat the received path
// compare, push to rdiff work stack if so

int send_mtimes( const char *base, int fd ) {
  FTS *fts;
  FTSENT *ent, *child;
  int current_depth = 0;

  struct dir_modified_t dir_info = {0};

  int opts = PEEPEE_POOPOO;

  fts = fts_open( base, opts, fn );
  if ( fts != NULL ) {
    while ( ( ent = fts_read(fts) ) != NULL ) {
      struct stat *s;

      if ( ent->fts_info == FTS_D ) {
        s = ent->fts_statp;
        dir_info.time = s.tv_sec;
        snprintf(dir_info.path, ent.fts_pathlen, "%s", ent->fts_path);

        send_data(&dir_info, sizeof(dir_info), fd);

      } else {
        continue;
      }


      switch ( ent->fts_info ) {
        case FTS_D:
          break;
        default:
          continue;
      }

    }
  }
}
