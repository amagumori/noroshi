#define MAX_FDS 100   //@FIXME

struct dir_record {
  struct list_head list;
  struct dirent entry;
  struct stat info;
  char path[PATH_MAX + 1];
};

typedef struct dir_record dir_record_t;

int ftw( const char *path,
         void *callback,
         int num_fds,
         int flags,
         void *anything ) {

  dir_record_t *record;
  dir_record_t *node;
  struct list_head *pos;
  struct list_head *safe_copy;

  char *current_dir = NULL;
  int retval = 0;
  int level = 0;
  int type = 0;

  if ( path == NULL ) return -1;
  if ( callback == NULL ) return -1;

  record = ( dir_record_t* )calloc(1, sizeof(*record));
  retval = ftw_stat_record(path, record, flags);

  if ( retval == -1 ) return -1;
  if ( !S_ISDIR(record->info.st_mode) ) return -1;

  if ( num_fds < 1 ) {
    num_fds = MAX_FDS;
  }

  snprintf( record->path, PATH_MAX, "%s", path );
  INIT_LIST_HEAD(&record->list);
  retval = ftw_recurse_path(path, &record->list);

  list_foreach( pos, safe_copy, &record->list ) {
    node = list_entry( pos, dir_record_t, list );
    retval = ftw_stat( node->path, node, flags );

    if ( retval == 0 ) {
      if ( S_ISDIR( node->info.st_mode ) ) {
        type = FTW_D;

        if ( flags & FTW_CHDIR ) {
          current_dir = (char*)calloc(1, PATH_MAX+1);
          getcwd( current_dir, PATH_MAX );
          chdir( node->filepath );
        }
      }
    }
  }


}
