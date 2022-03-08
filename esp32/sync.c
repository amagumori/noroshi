#include <stdio.h>
#include <string.h>
// ESP32 doesn't even have stat probably
#include <sys/stat.h>

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

int for_each_file( char *base_path, void *function ) {
  char path[1000];
  struct dirent *de;
  DIR *d = opendir(base_path);

  if ( !dir ) return;

  while ( (de = readdir(d)) != NULL ) {
    if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0 ) {

      // call the function pointer here...

      // tell if current d_name is a directory or not with stat?
      strcpy(path, basePath);
      strcat(path, "/");
      strcat(path, dp->d_name);
      for_each_file(path, function);
    }
  }

  closedir(dir);
}

int generate_signatures( const char *filename ) {
  struct stat buf;
  DIR *d;
  struct dirent *de;
  
  d = opendir(".");

  for ( de = readdir(d); de != NULL; de = readdir(d) ) {
    exists = stat( de->d_name, &buf );
    if ( exists > 0 ) {
      
    } else {

    }
  }
  closedir(d); 
}
