#define PASS_MAX 64 // ESP32 API pass-max is 64
#define NAME_MAX 64

typedef struct friend_t {
  u64 id;
  char name[NAME_MAX];
  char pass[PASS_MAX];
  struct friend_t *next;
  struct friend_t *prev;
} friend;

typedef struct friends_list_t {
  u8 count;   // ??
  struct friend_t *head;
  struct friend_t *tail;
} friends_list;


