#include <stdio.h>
#include <stdbool.h>

#define MAX_NAME_LENGTH 32
#define MAX_CONNECTED_USERS 32

typedef struct user {
  u32 id;
  const char public_name[MAX_PUBLIC_NAME_LENGTH];
  bool talking;
} user;

typedef struct session {
  const char session_name[MAX_NAME_LENGTH];
  user connected_users[MAX_CONNECTED_USERS];
} session;
