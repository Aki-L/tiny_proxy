#ifndef CONCURRENT_HASHMAP
#define CONCURRENT_HASHMAP

#include <pthread.h>

#define SEG_SIZE 7


typedef struct chmapentry{
        int key;
        int hash;
        int value;
        struct chmapentry *next;
} chmapentry_t;

typedef struct chmapseg{
        struct chmapentry entrys[SEG_SIZE];
        pthread_rwlock_t seg_lock;
} chmapseg_t;

typedef struct chmap{
	int capacity;
        size_t size;
        struct chmapseg *segs;
        pthread_rwlock_t chmap_lock;
} chmap_t;
chmap_t* hashmap_init(int capacity);
int hashmap_put(chmap_t *map, int key, int value);
chmapentry_t* hashmap_getentry(chmap_t *map, int key);
int hashmap_get(chmap_t *map, int key);
int hashmap_remove(chmap_t *map, int key);
int hashmap_containsKey(chmap_t *map, int key);
size_t hashmap_size(chmap_t *map);
void hashmap_destroy(chmap_t *map);
int hashmap_hash(int key);
int hashmap_putIfAbsent(chmap_t *map, int key, int value);
#endif
