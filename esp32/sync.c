#include <stdio.h>
#include <string.h>
// ESP32 doesn't even have stat probably
#include <sys/stat.h>
// it's in newlib
#include <ftw.h>

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

// we can assume directory structure will match starting at $BASEPATH for syncing.

// just hardcoding in BLAKE2 hashing
const rs_magic_number sig_magic = RS_BLAKE2_SIG_MAGIC;

int compare_versions( const char *path, const struct stat *stat, int flag ) {
  //struct time_t 
}

int check_versions ( const char *root, void *fn ) {
  int depth = 50;
  int ret = ftw( root, fn, depth );
}

int for_each_file( char *base_path, void *function ) {
  char path[1000];
  struct dirent *de;
  rs_result res;
  DIR *d = opendir(base_path);

  if ( !dir ) return;

  while ( (de = readdir(d)) != NULL ) {
    if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0 ) {

      // tell if current d_name is a directory or not with stat?
      strcpy(path, basePath);
      strcat(path, "/");
      strcat(path, dp->d_name);

      res = function(path);

      for_each_file(path, function);
    }
  }

  closedir(dir);
}

rs_result generate_signature( char *filepath ) {
  FILE *basis, *sig;
  rs_stats_t stats;
  rs_result result;
  // way more to it than this, handle 
  const char *sig_path = strcat( filepath, ".sig" );

  basis = rs_file_open(filepath, "rb");
  sig = rs_file_open(sig_path, "wb");

  const int block_size = 0; 
  const int strength = 0; 
  const int strong_len = 0;

  result = rs_sig_file(basis, sig, block_size, strong_len, sig_magic, &stats); 

  rs_file_close(sig);
  rs_file_close(basis);

  return result;
}

rs_result generate_delta( char *sig_path, char *other_path ) {
  // @FIXME this is not the way to generate delta filename
  const char delta_path = strcat(sig_path, ".delta");
  //
  FILE *sig_file = rs_file_open(sig_path, "rb");
  FILE *other_file = rs_file_open(other_file, "rb");
  FILE *delta_file = rs_file_open(delta_path, "wb");

  rs_result result;
  rs_signature_t *sumset;
  rs_stats_t stats;

  if ( ( result = rs_build_hash_table(sumset) ) != RS_DONE ) {
    return result;
  }

  // CHECK ARG ORDER
  result = rs_delta_file( sumset, other_file, delta_file, &stats );
  
  rs_file_close(delta_file);
  rs_file_close(other_file);
  rs_file_close(sig_file);

  rs_free_sumset(sumset);

  return result;
}

rs_result patch( 
