
int first_pass( char *base_path ) {
  FTS *fts;
  FTSENT *p, children;
  struct time_t latest = (time_t)0; // lol this doesn't work
  const char pathname[128];

  if ( ( fts = fts_open(base_path, fts_opts, NULL) ) == NULL ) {
    return -1;
  }
  
  child = fts_children(fts, 0);
  p = child->fts_parent;

  while ( child != NULL && child->fts_link != NULL ) {
    int t = time_newer( child->fs_statp.st_mtime, latest );
    if ( t > 0 ) {
      latest = child->fts_statp.st_mtime;
      strncpy( pathname, child->fts_path, child->fts_pathlen );
      child = child->fts_link;
    } else if ( t < 0 ) {
      child = child->fts_link;
    } else {
      child = child->fts_link;
    }
  }
}



int get_files_to_update( char *base_path, void *function ) {
  FTS *fts; 
  FTSENT *p, chp, parent;

  struct time_t latest = 0;
  const char pathname[PATH_MAX];

  if ( (fts = fts_open(base_path, fts_options, NULL)) == NULL ) {
    return -1;
  }

  while ( ( p = fts_read( fts ) ) != NULL ) {

    parent = p->fts_parent;
    children = fts_children(fts, 0 );

    while ( chp != NULL && chp->fts_link != NULL ) {
      if ( latest < chp->fts_statp.st_mtime ) {
        latest = chp->fts_statp.st_mtime;
        dir->modified = chp->fts_statp.st_mtime;
      }
    }
    
  }

  if ( !dir ) return;

  closedir(dir);
}


typedef struct dir_node_t {
  const char pathname[PATH_MAX];
  struct timespec modified;
  struct dir_modified_tree_t **children;
  size_t children_count;
} dir_node;

typedef struct dir_modified_t {
  const char pathname[PATH_MAX];
  struct timespec modified;
} dir_modified;

// root node of both trees should be initialized with timespec of 0 
// so modified check passes on first case.

int modified_tree( const char *path, dir_node *root, int depth ) {
  struct dirent *ent;
  DIR *d;
  char filename[128];
  dir_node_t *child;

  if ( depth == 0 ) return 1;
  if ( d = opendir(path) == NULL ) {
    return -1;
  }

  curr = ( dir_node_t * )calloc(1, sizeof(*dir_node));
  sprintf(curr.filepath, "%s", path);
  curr.modified = ( struct timespec ){0};

  while( ( ent = readdir(d)) != NULL ) {
    struct stat s;
    sprintf(filepath, "%s/%s", path, ent->d_name );
    if ( stat(filepath, &s) == -1 ) return -1;
    if ( (s.st_mode & S_IFMT) == S_IFDIR ) continue;

    if ( time_newer(s.st_mtimespec, curr.modified) ) {
      curr.modified = s.st_mtimespec;
    }
  }

  while ( ( ent = readdir(d) ) != NULL ) {
    struct stat s;
    sprintf(filepath, "%s/%s", path, ent->d_name);
    if ( stat(filepath, &s) == -1 ) return -1;
    if ( (s.st_mode & S_IFMT) == S_IFDIR ) {
      child = (dir_node_t *)calloc(1, sizeof(*dir_node));
      root->children = child;
      modified_tree(filepath, child, depth-1);
    }
  }
}

// pass in a pointer to where we're storing our dir / modified tuples so we can recurse

void dir_last_modified( char *path, struct tree *tree, int depth ) {
  struct dirent *ent;
  DIR *d;
  char filename[128];
  struct timespec latest = {0};
  struct dir_modified modified = {0};

  if ( depth == 0 ) return;

  if ( d = opendir( path ) == NULL ) {
    return -1;
  }

  // first iterate over all files to get newest modification
  while ( (ent = readdir(d)) != NULL ) {
    struct stat s;
    sprintf( filepath, "%s/%s", path, ent->d_name );
    if ( stat(filepath, &s) == -1 ) {
      return -1;
    }
    if ( (s.st_mode & S_IFMT) == S_IFDIR ) {
      continue;
    } else {
      if ( time_newer( s.st_mtimespec, latest ) ) {
        sprintf(modified.pathname, "%s", filepath);
        modified.timespec = s.st_mtimespec;
        latest = s.st_mtimespec;
      } else {
        continue;
      }
    }
  }

  send_queue_push( (void*)modified );

  while ( (ent = readdir(d)) != NULL ) {
    if ( (s.st_mode & S_IFMT) == S_IFDIR ) {
      dir_last_modified(filepath, tree, depth-1);
    }
  }
  return latest;
}

