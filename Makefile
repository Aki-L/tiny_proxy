CC = gcc
CFLAGS = -O0 -Wall -I .

# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LIB = -lpthread

all: tiny cgi proxy

proxy: proxy.c csapp.o threadpool.o
	$(CC) $(CFLAGS) -g3 -o proxy proxy.c csapp.o tools/threadpool.o $(LIB)

tiny: tiny.c csapp.o
	$(CC) $(CFLAGS) -g3 -o tiny tiny.c csapp.o $(LIB)

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c csapp.c

cgi:
	(cd cgi-bin; make)

threadpool.o:
	(cd tools; make)

clean:
	rm -f *.o tiny *~
	(cd cgi-bin; make clean)

