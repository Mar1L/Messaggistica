all: msg_client
CC = gcc
CFLAGS = -Wall
LDFLAGS = -lpthread

msg_client: msg_client.o
	$(CC) $(CFLAGS) msg_client.o -o msg_client $(LDFLAGS)

run1:
	./msg_client 10.0.2.15 4444 10.0.2.15 1234
run2:
	./msg_client 10.0.2.15 5555 10.0.2.15 1234

clean: 
	rm *.o msg_client
	
	
