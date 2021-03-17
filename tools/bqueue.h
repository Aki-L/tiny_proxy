#ifndef BQUEUE
#define BQUEUE

typedef struct bqueue_entry{
        int data;
        struct bqueue_entry *next;
} bqueue_entry_t;

typedef struct queue{
        bqueue_entry_t head;
        bqueue_entry_t *tail;
        size_t size;
} queue_t;
typedef struct bqueue{
        queue_t queue;
        pthread_mutex_t qlock;
        pthread_cond_t isnempty;
        int shuttingdown;
} bqueue_t;

void *test_remove(void* args);
void *test_insert(void *args);
bqueue_t *bqueue_init();
int bqueue_destroy(bqueue_t *bqueue);
int bqueue_insert(bqueue_t *bqueue, int data);
int isempty(queue_t *queue);
int bqueue_remove(bqueue_t *bqueue);
int enqueue(queue_t *queue, bqueue_entry_t *entry);
bqueue_entry_t *dequeue(queue_t *queue);
#endif
