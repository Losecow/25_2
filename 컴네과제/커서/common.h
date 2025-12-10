#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEFAULT_PORT 4000
#define BUFFER_SIZE 1024
#define BACKLOG 16

int send_all(int fd, const void *buf, size_t len);
int recv_all(int fd, void *buf, size_t len);
ssize_t recv_line(int fd, char *buf, size_t maxlen);
int create_server_socket(uint16_t port, int backlog);
int connect_to_server(const char *ip, uint16_t port);

#endif /* COMMON_H */

