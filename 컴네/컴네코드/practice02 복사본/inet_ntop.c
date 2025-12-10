#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

int main(int argc, char *argv[])
{
    struct sockaddr_in addr1, addr2;
    char str_arr1[INET_ADDRSTRLEN];
    char str_arr2[INET_ADDRSTRLEN];

    addr1.sin_addr.s_addr = htonl(0x01020304);
    addr2.sin_addr.s_addr = htonl(0x01010101);

    if (inet_ntop(AF_INET, &addr1.sin_addr, str_arr1, sizeof(str_arr1)) == NULL) {
        perror("inet_ntop error");
        return 1;
    }

    if (inet_ntop(AF_INET, &addr2.sin_addr, str_arr2, sizeof(str_arr2)) == NULL) {
        perror("inet_ntop error");
        return 1;
    }

    printf("Dotted-Decimal notation1: %s \n", str_arr1);
    printf("Dotted-Decimal notation2: %s \n", str_arr2);
    
    return 0;
}
