#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "bqueue.h"
#include <assert.h>
/*
typedef struct queue{
	bqueue_entry_t head;
	bqueue_entry_t *tail;
	size_t size;
} queue_t;

typedef struct bqueue_entry{
	int data;
	struct bqueue_entry *next;
} bqueue_entry_t;

typedef struct bqueue{
	queue_t queue;
	pthread_mutex_t qlock;
	pthread_cond_t isnempty;
	int shuttingdown;
} bqueue_t;
*/

void *test_remove(void* args);
void *test_insert(void *args);
bqueue_t *bqueue_init();
int bqueue_destroy(bqueue_t *bqueue);
int bqueue_insert(bqueue_t *bqueue, int data);
int isempty(queue_t *queue);
int bqueue_remove(bqueue_t *bqueue);
int enqueue(queue_t *queue, bqueue_entry_t *entry);
bqueue_entry_t *dequeue(queue_t *queue);
int bqueue_remove_nonblocking(bqueue_t *bqueue);
/*
int main(){
	bqueue_t *bqueue = bqueue_init();
	pthread_t thread1, thread2;
	pthread_create(&thread1, NULL, test_insert, (void *)bqueue);
	pthread_create(&thread2, NULL, test_remove, (void *)bqueue);
	pthread_join(thread1, NULL);
	pthread_join(thread2, NULL);
	bqueue_destroy(bqueue);
	return 0;
}
*/
void *test_remove(void* args){
	bqueue_t *bqueue = (bqueue_t *)args;
	for(int i = 0 ; i < 10; i++){
		int data = bqueue_remove(bqueue);
		printf("[bqueue] remove: %d\n", data);
	}
}
void *test_insert(void *args){
	bqueue_t *bqueue = (bqueue_t *)args;
	for(int i = 0 ; i < 10; i++){
		bqueue_insert(bqueue, i);
		printf("[bqueue] insert: %d\n", i);
	}
}

bqueue_t *bqueue_init(){
	bqueue_t *bqueue = (bqueue_t *)malloc(sizeof(bqueue_t));
	pthread_mutex_init(&(bqueue->qlock), NULL);
	pthread_cond_init(&(bqueue->isnempty), NULL);
	return bqueue;
}

int bqueue_destroy(bqueue_t *bqueue){
	bqueue_entry_t *entry = (bqueue->queue).head.next, *oldentry;
	pthread_mutex_lock(&(bqueue->qlock));
	bqueue->shuttingdown = 1;
	pthread_cond_broadcast(&(bqueue->isnempty));
	pthread_mutex_unlock(&(bqueue->qlock));
	while(entry!=NULL){
		oldentry = entry;
		entry = entry->next;
		free(oldentry);
	}
	free(bqueue);
}

int bqueue_insert(bqueue_t *bqueue, int data){
	bqueue_entry_t *entry = (bqueue_entry_t *)malloc(sizeof(bqueue_entry_t));
	entry->data = data;
	pthread_mutex_lock(&(bqueue->qlock));
	enqueue(&(bqueue->queue), entry);
	pthread_mutex_unlock(&(bqueue->qlock));
	pthread_cond_signal(&(bqueue->isnempty));
	return 1;
}

int isempty(queue_t *queue){
	return queue->size==0;
}

int bqueue_remove_nonblocking(bqueue_t *bqueue){
	int data;
        assert(bqueue!=NULL);
        bqueue_entry_t *entry;
        pthread_mutex_lock(&(bqueue->qlock));
        if(bqueue->shuttingdown){
                pthread_mutex_unlock(&(bqueue->qlock));
                return -1;
        }
	if(isempty(&(bqueue->queue))){
		pthread_mutex_unlock(&(bqueue->qlock));
		return -2;
	}
        entry = dequeue(&(bqueue->queue));
        pthread_mutex_unlock(&(bqueue->qlock));
        if(entry==NULL) return -1;
        data = entry->data;
        free(entry);
        return data;
}

int bqueue_remove(bqueue_t *bqueue){
	int data;
	assert(bqueue!=NULL);
	bqueue_entry_t *entry;
	pthread_mutex_lock(&(bqueue->qlock));
	while(isempty(&(bqueue->queue))&&(!bqueue->shuttingdown)){
		pthread_cond_wait(&(bqueue->isnempty), &(bqueue->qlock));
	}
	if(bqueue->shuttingdown){
		pthread_mutex_unlock(&(bqueue->qlock));
		return -1;
	}
	entry = dequeue(&(bqueue->queue));
	pthread_mutex_unlock(&(bqueue->qlock));
	if(entry==NULL) return -1;
	data = entry->data;
	free(entry);
	return data;
}

int enqueue(queue_t *queue, bqueue_entry_t *entry){
	if(queue->tail==NULL){
		queue->tail = entry;
		(queue->head).next = entry;
	}else{
		queue->tail->next = entry;
		queue->tail = entry;
	}
	queue->size++;
	return 1;
}

bqueue_entry_t *dequeue(queue_t *queue){
	if(queue->tail==NULL){
		return NULL;
	}else{
		bqueue_entry_t *entry = (queue->head).next;
		(queue->head).next = entry->next;
		if(entry==queue->tail) queue->tail = NULL;
		entry->next = NULL;
		queue->size--;
		return entry;
	}
}
