#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

int main(int argc, char *argv[])
{
    struct addrinfo hints, *res, *p;
    int err;
    char ipstr[INET_ADDRSTRLEN];

    if (argc != 2) {
        printf("Usage: %s <hostname>\n", argv[0]);
        exit(1);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;        // IPv4 only
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_flags = AI_CANONNAME;    // request canonical name

    if ((err = getaddrinfo(argv[1], NULL, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        exit(1);
    }

    if (res->ai_canonname)
        printf("Official name: %s\n", res->ai_canonname);

    int i = 0;
    for (p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *addr = (struct sockaddr_in *)p->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, ipstr, sizeof(ipstr));
        printf("IP addr %d: %s\n", ++i, ipstr);
    }

    freeaddrinfo(res);
    return 0;
}
