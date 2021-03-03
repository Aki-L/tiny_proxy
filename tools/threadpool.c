#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "threadpool.h"

/**
 *
 *	typedef struct{
        void*(*function)(void*);
        void* args;
        threadpool_task_t *next;
} threadpool_task_t;

typedef struct{
        pthread_t *threads;
	pthread_t adjust;
        threadpool_task_t task_header;
        threadpool_tast_t *task_tail_p;
        int threadpool_size;
	pthread_mutex_t pool_lock;
	pthread_cond_t task_not_empty;
	int isshuttingdown;
} threadpool_t;
 * 
 * **/

#define WAITTING_TIME_ADJUST 10

/**
 *@function threadpool_handle(void *pool) the job which working thread in this pool doing
 *@param pool the pool own this thread
 * **/
void *threadpool_handle(void *threadpool);

threadpool_t *threadpool_create(int threadpool_size);

int threadpool_destroy(void *threadpool);

int threadpool_add(void *threadpool, void*(*function)(void *), void * args);

void* threadpool_adjust(void *threadpool);


threadpool_t *threadpool_create(int threadpool_size){
	threadpool_t *pool = NULL;
	do{
		if((pool=malloc(sizeof(threadpool_t)))==NULL){
			printf("initialization failed, error when malloc threadpool\n");
			break;
		}
		if((pool->threads = (pthread_t *)calloc(threadpool_size, sizeof(pthread_t)))==NULL){
			printf("initialization failed, error when malloc threads\n");
			break;
		}
		pool->isshuttingdown = 0;
		pool->threadpool_size = threadpool_size;
		pool->task_tail_p = NULL;

		if((pthread_mutex_init(&(pool->pool_lock), NULL))!=0){
			printf("pthread mutex init error\n");
			break;
		}
		if((pthread_cond_init(&(pool->task_not_empty), NULL))!=0){
			printf("pthread cond init error\n");
			break;
		}

		for(int i = 0; i < threadpool_size; i++){
			pthread_create(&(pool->threads[i]), NULL, threadpool_handle, pool);
			printf("start thread 0x%x...\n", pool->threads[i]);
		}
		pthread_create(&(pool->adjust), NULL, threadpool_adjust, pool);
		printf("start adjust thread 0x%x...\n", &(pool->adjust));
		return pool;
	}while(0);
	threadpool_destroy(pool);
	return NULL;
}

int threadpool_destroy(void *threadpool){
	threadpool_t *pool = (threadpool_t *)threadpool;
	do{
		if(pool==NULL){
			printf("no existing pool, return from destroy...\n");
			break;
		}
		pthread_mutex_lock(&(pool->pool_lock));
		pool->isshuttingdown = 1;
		pthread_mutex_unlock(&(pool->pool_lock));
		pthread_cond_broadcast(&(pool->task_not_empty));
		for(int i = 0 ; i < pool->threadpool_size; i++){
			pthread_join(((pool->threads)[i]), NULL);
		}
		pthread_join((pool->adjust), NULL);

		if(pool==NULL) break;
		if(pool->threads){
			free(pool->threads);
			pthread_mutex_destroy(&(pool->pool_lock));
			pthread_cond_destroy(&(pool->task_not_empty));
		}
		free(pool);
		return 0;
	}while(0);
	return -1;
}

void* threadpool_handle(void *threadpool){

	threadpool_t *pool = (threadpool_t *)threadpool;
	while(true){
		pthread_mutex_lock(&(pool->pool_lock));
		while((pool->task_tail_p==NULL)&&(pool->isshuttingdown==false)){
			pthread_cond_wait(&(pool->task_not_empty), &(pool->pool_lock));
		}
		
		if(pool->isshuttingdown){
			printf("thread 0x%x is exiting...\n", pthread_self());
			pthread_mutex_unlock(&(pool->pool_lock));
			pthread_exit(NULL);
		}

		threadpool_task_t *task = (pool->task_header).next;
		assert((pool->task_header).next!=NULL);
		if(task==NULL) continue;
		if(task==pool->task_tail_p){
			pool->task_tail_p = NULL;
		}
		(pool->task_header).next = (*task).next;
		pthread_mutex_unlock(&(pool->pool_lock));

		printf("thread 0x%x is excuting task 0x%x...\n", pthread_self(), task);
		(*((*task).function))((*task).args);
		free(task);
	}
	pthread_exit(NULL);
	return NULL;
}

int threadpool_add(void *threadpool, void*(*function)(void *), void * args){
	threadpool_t *pool = (threadpool_t *)threadpool;
	assert(function!=NULL);
	assert(args!=NULL);
	assert(pool!=NULL);
	threadpool_task_t *newtask = (threadpool_task_t *)malloc(sizeof(threadpool_task_t));
	newtask->function = function;
	newtask->args = args;

	pthread_mutex_lock(&(pool->pool_lock));
	if(pool->task_tail_p==NULL){
		printf("inserting new task in a empty list...\n");
		pool->task_tail_p = newtask;
		pool->task_header.next = newtask;
	}else{
		printf("inserting new task in a not empty list...\n");
		(pool->task_tail_p)->next = newtask;
		pool->task_tail_p = newtask;
	}
	pthread_cond_signal(&(pool->task_not_empty));
	pthread_mutex_unlock(&(pool->pool_lock));
	return 0;
}

/**
 *
 *	todo--- a new function that check if the task list is empty, since when adding a task, it could happened that 
 *	no available thread receive the signal, causing the task stuck in the list.
 * **/
void* threadpool_adjust(void *threadpool){
	threadpool_t *pool = (threadpool_t *)threadpool;
	while(!pool->isshuttingdown){
		sleep(WAITTING_TIME_ADJUST);
		pthread_mutex_lock(&(pool->pool_lock));
		if((pool->task_tail_p)!=NULL){
			pthread_cond_signal(&(pool->task_not_empty));
		}
		pthread_mutex_unlock(&(pool->pool_lock));
	}
}
