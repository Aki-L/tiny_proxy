CC = gcc
OBJS = threadpool.o
CFLAGS = -O -Wall

LIB = -lpthread

all : threadpool.o concurrent_hashmap

threadpool.o : threadpool.h
	$(CC) $(CFLAGS) -g3 -c threadpool.c $(LIB)
	
concurrent_hashmap : concurrent_hashmap.h
	$(CC) $(CFLAGS) -g3 -o concurrent_hashmap concurrent_hashmap.c $(LIB)
