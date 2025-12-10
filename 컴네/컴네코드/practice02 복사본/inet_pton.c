#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

int main() {
    const char *ip1 = "1.2.3.4";
    const char *ip2 = "239.1.2.3";
    struct in_addr addr1, addr2; 

    if (inet_pton(AF_INET, ip1, &addr1) <= 0)
        perror("inet_pton IPv4 error");
    else
        printf("IP#1's binary: 0x%08x\n", addr1.s_addr);

    if (inet_pton(AF_INET, ip2, &addr2) <= 0)
        perror("inet_pton IPv4 error");
    else
        printf("IP#2's binary: 0x%08x\n", addr2.s_addr);

    return 0;
}