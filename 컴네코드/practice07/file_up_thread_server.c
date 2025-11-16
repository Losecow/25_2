#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define FILE_LEN 32
#define BUF_SIZE 1024
void error_handling(char *message);
void *handle_client(void *arg);

int main(int argc, char *argv[])
{
	int serv_sd, clnt_sd;
	pthread_t thread; 
	
	struct sockaddr_in serv_adr, clnt_adr;
	socklen_t clnt_adr_sz;
	
	if (argc != 2) {
		printf("Usage: %s <port>\n", argv[0]);
		exit(1);
	}
	
	serv_sd = socket(PF_INET, SOCK_STREAM, 0);   
	
	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family = AF_INET;
	serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_adr.sin_port = htons(atoi(argv[1]));
	
	if (bind(serv_sd, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
		error_handling("bind() error");
	if (listen(serv_sd, 5) == -1)
		error_handling("listen() error");

	while (1)
	{
		clnt_adr_sz = sizeof(clnt_adr);    
		clnt_sd = accept(serv_sd, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);	

		// TODO: pthread_create & detach 


		printf("Connected client IP(sock=%d): %s \n", clnt_sd, inet_ntoa(clnt_adr.sin_addr));
	}
	
	close(serv_sd);
	return 0;
}

void *handle_client(void *arg)
{
	// TODO: file receiving 
	
	return NULL;
}

void error_handling(char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}
