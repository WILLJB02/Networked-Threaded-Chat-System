CC = gcc
CFLAGS = -Wall -pthread -pedantic -std=gnu99 -g
.PHONY: all clean
.DEFAULT_GOAL := all

all: client server


clean:
	rm server client
	rm *.o

client: client.o shared.o
	$(CC) $(CFLAGS) $^ -o $@

client.o: client.c shared.h

server: server.o shared.o
	$(CC) $(CFLAGS) $^ -o $@

server.o: server.c shared.h

shared.o: shared.c shared.h
