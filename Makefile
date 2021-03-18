CC = gcc
CFLAGS = -O0 -Wall -I .

# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LIB = -lpthread

all: tiny cgi proxy

proxy: proxy.c csapp.o threadpool.o concurrent_hashmap.o parse_requestline.o bqueue.o
	$(CC) $(CFLAGS) -g3 -o proxy proxy.c csapp.o tools/threadpool.o tools/concurrent_hashmap.o tools/parse_requestline.o tools/bqueue.o $(LIB)

tiny: tiny.c csapp.o
	$(CC) $(CFLAGS) -g3 -o tiny tiny.c csapp.o $(LIB)

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c csapp.c

cgi:
	(cd cgi-bin; make)

threadpool.o concurrent_hashmap.o parse_requestline.o bqueue.o:
	(cd tools; make)

clean:
	rm -f *.o tiny *~ proxy
	(cd cgi-bin; make clean)
	(cd tools; make clean)

