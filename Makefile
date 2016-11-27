#MAKEFILE
CC = gcc
CFLAGS = -Wall 
LFLAGS = -pthread

.PHONY: clean

all: supervisor server client

supervisor: supervisor.c
	$(CC) $(CFLAGS) -o $@ $< $(LFLAGS) 

server: server.c
	$(CC) $(CFLAGS) -o $@ $< $(LFLAGS)  

client: client.c
	$(CC) $(CFLAGS) -o $@ $< 

test: all
	@./test 10 5 8 20 	

clean:
	rm supervisor server client client_* supervisor_* 
