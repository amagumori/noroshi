#include <stdlib.h>
#include <string.h>
#include "types.h"

typedef struct {
  char *key;
  char *value;
} ht_item;

typedef struct {
  int size;
  int count;
  ht_item **items;
} ht_table;

static const u32 seedA = 0xCAFEBABE;
static const u32 seedB = 0xDEADF00D;

static inline u32 rotl32 ( u32 x, s8 r ) {
  return (x << r) | (x >> (32 - r));
}

static inline u32 fmix32 (u32 h) {
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;

  return h;
}

#define ROTL32(x,y) rotl32(x,y)

#define getblock(p,i) (p[i])

void murmur3_32 (const void* key, int len, u32 seed, void* out) {
  const u8* data = (const u8*)key;
  const int nblocks = len / 4;

  u32 h1 = seed;
  u32 c1 = 0xcc9e2d51;
  u32 c2 = 0x1b873593;

  // body
  const u32* blocks = (const u32*)(data + nblocks*4);
  
  for (int i = -nblocks; i; i++) {
    u32 k1 = getblock(blocks, i);
    k1 *= c1;
    k1 = ROTL32(k1, 15);
  
    h1 ^= k1;
    h1 = ROTL32(h1, 13);
    h1 = h1 * 5 + 0xe6546b64;
  }

  // tail
  const u8* tail = (const u8*)(data + nblocks*4);
  u32 k1 = 0;
  switch (len & 3) {
    case 3: k1 ^= tail[2] << 16;
    case 2: k1 ^= tail[1] << 8;
    case 1: k1 ^= tail[0];
            k1 *= c1; k1 = ROTL32(k1, 15); k1 *= c2; h1 ^= k1;
  };

  h1 ^= len;
  h1 = fmix32(h1);

  *(u32*)out = h1;
}

static int ht_get_hash( const char *s, const int num_buckets, const int attempt ) {
  u32 a, b = 0;
  murmur3_32( s, seedA, num_buckets, a );
  murmur3_32( s, seedB, num_buckets, b );
  return ( a + ( attempt * ( b + 1 ) ) ) % num_buckets;
}

static ht_item *ht_new_item( const char *k, const char *v ) { 
  ht_item *item = malloc(sizeof(ht_item));
  item->key = strdup( k );
  item->value = strdup( v );
  return item;
}

ht_table *new_ht( void ) {
  ht_table *ht = malloc(sizeof(ht_table));
  ht->size = 50;
  ht->count = 0;
  ht->items = calloc( (size_t)ht->size, sizeof(ht_item*) );
  return ht;
}

static void ht_delete_item( ht_item *i ) {
  free(i->key);
  free(i->value);
  free(i);
}

void delete_ht( ht_table *ht ) {
  for ( int i=0; i < ht->size; i++ ) {
    ht_item *item = ht->items[i];
    if ( item != NULL ) {
      ht_delete_item(item)
    }
  }
  free(ht->items);
  free(ht);
}

void ht_insert( ht_table *ht, const char *key, const char *value ) {
  ht_item *item = ht_new_item( key, value );
  int index = ht_get_hash( item->key, ht->size, 0 );
  ht_item *current_item = ht->items[index];
  int i = 1;
  while ( current_item != NULL ) {
    if ( current_item != &HT_DELETED_ITEM ) {
      if ( strcmp(current_item->key, key) == 0 ) {
        ht_delete_item(current_item);
        ht->items[index] = item;
        return;
      }
      index = ht_get_hash( item->key, ht->size, i );
      current_item = ht->items[index];
      i++;
    }
  }
  ht->items[index] = item;
  ht->count++;
}

char *ht_search( ht_table *ht, const char *key ) {
  int index = ht_get_hash( key, ht->size, 0 );
  ht_item *item = ht->items[index];
  int i = 1;
  while ( item != NULL ) {
    if ( item != &HT_DELETED_ITEM ) {
      if ( strcmp( item->key, key ) == 0 ) {
        return item->value;
      }
      index = ht_get_hash(key, ht->size, i);
      item = ht->items[index];
      i++;
    }
  }
  return NULL;
}

void ht_delete( ht_table *ht, const char *key ) {
  int index = ht_get_hash( key, ht->size, 0 );
  ht_item *item = ht->items[index];
  int i = 1;
  while ( item != NULL ) {
    if ( item != &HT_DELETED_ITEM ) {
      if ( strcmp( item->key, key ) == 0 ) {
        ht_delete_item(item);
        ht->items[index] = &HT_DELETED_ITEM;
      }
    }
    index = ht_get_hash( key, ht->size, i );
    item = ht->items[index];
    i++;
  }
  ht->count--;
}
