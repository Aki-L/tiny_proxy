#ifndef T_POOL
#define T_POOL

#include <stdbool.h>
/**
 *
 *	A thread pool implements basic pooling functions
 *
 * **/
typedef struct threadpool_task{
        void*(*function)(void*);
        void* args;
        struct threadpool_task *next;
} threadpool_task_t;

typedef struct threadpool{
        pthread_t *threads;
        pthread_t adjust;
        struct threadpool_task task_header;
        struct threadpool_task *task_tail_p;
        int threadpool_size;
        pthread_mutex_t pool_lock;
        pthread_cond_t task_not_empty;
        int isshuttingdown;
} threadpool_t;
  


void *threadpool_handle(void *threadpool);
    
threadpool_t *threadpool_create(int threadpool_size);

int threadpool_destroy(void *threadpool);

int threadpool_add(void *threadpool, void*(*function)(void *), void * args);

void* threadpool_adjust(void *threadpool);

#endif
