all:  msg_server
CC = gcc
CFLAGS = -Wall
	
msg_server: msg_server.o
	$(CC) $(CFLAGS) msg_server.o -o msg_server

run:
	./msg_server 1234
clean: 
	rm *.o msg_server
