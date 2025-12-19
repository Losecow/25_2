#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 1024
void error_handling(char *message);

int main(int argc, char *argv[])
{
	int serv_sock, clnt_sock;
	char message[BUF_SIZE];
	int str_len;
	
	struct sockaddr_in serv_adr;
	struct sockaddr_in clnt_adr;
	socklen_t clnt_adr_sz;
	
	if (argc != 2) {
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);
	}
	
	// TODO: socket()
	serv_sock = socket();
	if (serv_sock == -1)
		error_handling("socket() error");

	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family = AF_INET;
	serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_adr.sin_port = htons(atoi(argv[1]));

	// TODO: bind()
	if (bind())
		error_handling("bind() error");
		
	printf("Server Listen...\n");

	// TODO: listen()
	if (listen())
		error_handling("listen() error");
	
	clnt_adr_sz = sizeof(clnt_adr);
 	// TODO: accept()
	clnt_sock = accept();
	if (clnt_sock == -1)
		error_handling("accept() error");
	else
		puts("Client is connected!");
	
	// TODO: read message from client
	while ((str_len = read()) != 0)
	{
		message[str_len] = 0;
		printf("Client's message: %s", message);
		// TODO: write message to client 
		write();
	}

	close(clnt_sock);
	close(serv_sock);
	return 0;
}

void error_handling(char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}