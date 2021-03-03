CC = gcc
OBJS = threadpool.o
CFLAGS = -O -Wall

LIB = -lpthread

all : threadpool.o

threadpool.o : threadpool.h
	$(CC) $(CFLAGS) -g3 -c threadpool.c $(LIB)
	

