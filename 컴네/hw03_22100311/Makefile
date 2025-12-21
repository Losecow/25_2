CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS = -lpthread

CLIENT = client
SERVER = server

all: $(CLIENT) $(SERVER)

$(CLIENT): client_config.o
	$(CC) $(CFLAGS) -o $(CLIENT) client_config.o

$(SERVER): server_config.o
	$(CC) $(CFLAGS) -o $(SERVER) server_config.o $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CLIENT) $(SERVER) *.o

.PHONY: all clean
