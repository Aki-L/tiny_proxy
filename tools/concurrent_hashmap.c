#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
/*
typedef struct chmapentry{
        int key;
        int hash;
        int value;
        struct chmapentry *next;
} chmapentry_t;

typedef struct chmapseg{
        struct chmapentry entrys[SEG_SIZE];
        pthread_mutex_t seg_lock;
} chmapseg_t;

typedef struct chmap{
        size_t size;
        struct chmapseg *segs;
        pthread_mutex_t chmap_lock;
} chmap_t;
*/
chmap_t* hashmap_init(int capacity);
void hashmap_put(chmap_t *map, int key, int value);
chmapentry_t* hashmap_getentry(chmap_t *map, int key);
int hashmap_get(chmap_t *map, int key);
int hashmap_remove(chmap_t *map, int key);
int hashmap_containskey(chmap_t *map, int key);
size_t hashmap_size(chmap_t *map);
void hashmap_destroy(chmap_t *map);

int hash_capacity;

chmap_t *hashmap_init(int capacity){
	do{
		size_t nmemb = (capacity/SEG_SIZE)+1;
		hash_capacity = nmemb*SEG_SIZE;
		if((chmapseg_t *segs = (chmapseg_t *)calloc(nmemb, sizeof(chmapseg_t)))==NULL){
			perror("malloc: hashmap segments");
			break;
		}
		for(int i = 0 ; i < nmemb; i++){
			pthread_mutex_init(&(segs[i].seg_lock), NULL);
		}

		if((chmap_t *map = (chmap_t *)malloc(sizeof(chmap_t)))==NULL){
			perror("malloc: hashmap");
			free(segs);
			break;
		}
		map->size = 0;
		map->segs = segs;
		pthread_mutex_init(&(map->chmap_lock), NULL);
		return map;
	}while(0);
	return NULL;
}

void hashmap_destroy(chmap_t *map){
	chmapseg_t *seg;
	chmapentry_t *entry, *tmp;
	for(int i = 0 ; i < capacity/SEG_SIZE+1; i++){
		seg = &(map->segs[i]);
		for(int j = 0 ; j < SEG_SIZE; j++){
			entry = &(seg->entrys[j]);
			entry = entry->next;
			while(entry!=NULL){
				tmp = entry->next;
				free(entry);
				entry = tmp;
			}
		}
		pthread_mutex_destroy(&(seg->seg_lock));
	}
	pthread_mutex_destroy(&(map->chmap_lock));
}

int hash(int key){
	return key%hash_capacity;
}

void hashmap_put(chmap_t *map, int key, int value){
	chmapentry_t *entry = hashmap_getentry(map, key);
	int hash = hash(key);
	chmapseg_t *seg = (map->segs)+hash/SEG_SIZE;
	chmapentry_t *entryhead = &(seg->entrys[hash%SEG_SIZE]);
	if(entry==NULL){
		entry = (chmapentry_t *)malloc(sizeof(chmapentry_t));
		entry->key = key;
		entry->value = value;
		entry->hash = hash;
		entry->next = NULL;
		pthread_mutex_lock(&(seg->seg_lock));
		while(entryhead->next!=NULL){
			entryhead = entryhead->next;
		}
		entryhead->next = entry;
		map->size++;
	}else{
		pthread_mutex_lock(&(seg->seg_lock));
		entry->value = value;
	}
	pthread_mutex_unlock(seg->seg_lock);
}

chmapentry_t *hashmap_getentry(chmap_t *map, int key){
	int hash = hash(key);
	chmapseg_t *seg = (map->segs)+hash/SEG_SIZE;
	chmapentry_t *entryhead = &(seg->entrys[hash%SEG_SIZE]);
	while(entryhead != NULL){
		entryhead = entryhead->next;
		if(entryhead->key == key){
			return entryhead;
		}
	}
	return NULL;
}

int hashmap_get(chmap_t *map, int key){
	chmapentry_t *entry = hashmap_getentry(map, key);
	if(entry==NULL){
		perror("no such key to get");
		return -1;
	}else{
		return entry->value;
	}
}

int hashmap_remove(chmap_t *map, int key){
	int hash = hash(key);
	chmapseg_t *seg = (map->segs)+hash/SEG_SIZE;
	chmapentry_t *entryhead = &(seg->entrys[hash%SEG_SIZE]);
	while((entryhead->next!=NULL) && ((entryhead->next)->key!=key)){
		entryhead = entryhead->next;
	}
	if((entryhead->next)->key == key){
		pthread_mutex_lock(&(seg->seg_lock));
		chmapentry_t *target = entryhead->next;
		entryhead->next = target->next;
		map->size--;
		free(target);
		pthread_mutex_unlock(&(seg->seg_lock));
		return 0;
	}else{
???LINES MISSING
