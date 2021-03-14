CC = gcc
OBJS = threadpool.o
CFLAGS = -O0 -Wall

LIB = -lpthread

all : threadpool.o concurrent_hashmap.o parse_requestline.o 

threadpool.o : threadpool.h
	$(CC) $(CFLAGS) -g3 -c threadpool.c $(LIB)
	
concurrent_hashmap.o : concurrent_hashmap.h
	$(CC) $(CFLAGS) -g0 -c concurrent_hashmap.c $(LIB)

parse_requestline.o: parse_requestline.h
	$(CC) $(CFLAGS) -g -c parse_requestline.c $(LIB)

clean:
	rm -f *.o concurrent_hashmap parse_requestline
