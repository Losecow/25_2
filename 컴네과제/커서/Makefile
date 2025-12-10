CC      := gcc
CFLAGS  := -Wall -Wextra -pedantic -std=c11
LDFLAGS :=

TARGETS := server client

.PHONY: all clean

all: $(TARGETS)

OBJS := common.o

server: server.c common.h $(OBJS)
	$(CC) $(CFLAGS) -o $@ server.c $(OBJS) $(LDFLAGS)

client: client.c common.h $(OBJS)
	$(CC) $(CFLAGS) -o $@ client.c $(OBJS) $(LDFLAGS)

common.o: common.c common.h
	$(CC) $(CFLAGS) -c common.c

clean:
	rm -f $(TARGETS) $(OBJS)

