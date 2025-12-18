// 표준 입출력, 메모리 관리, 문자열 처리 함수
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// 유닉스 표준 함수 (read, write, close 등)
#include <unistd.h>
// 정수형 포인터 변환에 필요한 헤더
#include <stdint.h>
// 인터넷 주소 변환 함수 (inet_ntoa 등)
#include <arpa/inet.h>
// 소켓 관련 함수
#include <sys/socket.h>
// 스레드 관련 함수
#include <pthread.h>

// 파일명 최대 길이
#define FILE_LEN 32
// 데이터 전송 버퍼 크기
#define BUF_SIZE 1024

// 함수 선언
void error_handling(char *message);
void *handle_client(void *arg);

int main(int argc, char *argv[])
{
	int serv_sd, clnt_sd;  // 서버 소켓 디스크립터, 클라이언트 소켓 디스크립터
	pthread_t thread;       // 스레드 ID를 저장할 변수
	
	// 서버와 클라이언트의 주소 정보를 저장할 구조체
	struct sockaddr_in serv_adr, clnt_adr;
	socklen_t clnt_adr_sz;  // 클라이언트 주소 구조체의 크기
	
	// 명령행 인자 검사: 포트 번호가 필요함
	if (argc != 2) {
		printf("Usage: %s <port>\n", argv[0]);
		exit(1);
	}
	
	// TCP 소켓 생성 (PF_INET: IPv4, SOCK_STREAM: TCP)
	serv_sd = socket(PF_INET, SOCK_STREAM, 0);   
	
	// 서버 주소 구조체 초기화 및 설정
	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family = AF_INET;                    // IPv4 주소 체계
	serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);     // 모든 네트워크 인터페이스에서 연결 허용
	serv_adr.sin_port = htons(atoi(argv[1]));         // 포트 번호 설정 (네트워크 바이트 순서로 변환)
	
	// 소켓에 주소 바인딩 (서버 주소와 소켓 연결)
	if (bind(serv_sd, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
		error_handling("bind() error");
	
	// 소켓을 수신 대기 상태로 설정 (최대 5개의 연결 요청 대기)
	if (listen(serv_sd, 5) == -1)
		error_handling("listen() error");

	// 무한 루프: 클라이언트 연결을 계속 받음
	while (1)
	{
		clnt_adr_sz = sizeof(clnt_adr);    
		// 클라이언트 연결 요청 수락 (새로운 소켓 디스크립터 반환)
		clnt_sd = accept(serv_sd, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);	

		pthread_create(&thread, NULL, handle_client, (void *)(intptr_t)clnt_sd);
		pthread_detach(thread);

		// 연결된 클라이언트 정보 출력
		printf("Connected client IP(sock=%d): %s \n", clnt_sd, inet_ntoa(clnt_adr.sin_addr));
	}
	
	close(serv_sd);  // 서버 소켓 닫기 (실제로는 여기 도달하지 않음)
	return 0;
}

// 클라이언트와의 통신을 처리하는 스레드 함수
void *handle_client(void *arg)
{
	int clnt_sd = (int)(intptr_t)arg;
	FILE *fp;                      // 파일 포인터
	char file_name[FILE_LEN];      // 받을 파일명 저장 버퍼
	char buf[BUF_SIZE];            // 데이터 수신 버퍼
	int read_cnt;                  // 실제로 읽은 바이트 수
	
	// 클라이언트로부터 파일명 수신
	read(clnt_sd, file_name, FILE_LEN);
	
	// 받은 파일명으로 파일 열기 (바이너리 쓰기 모드)
	fp = fopen(file_name, "wb");
	if (fp == NULL)
	{
		// 파일 열기 실패 시 소켓 닫고 종료
		close(clnt_sd);
		return NULL;
	}
	
	printf("Received File name: %s \n", file_name);
	
	// 클라이언트로부터 파일 데이터를 계속 수신하여 파일에 저장
	// read()가 0을 반환하면 더 이상 읽을 데이터가 없음 (연결 종료)
	while ((read_cnt = read(clnt_sd, buf, BUF_SIZE)) != 0)
	{
		// 읽은 데이터를 파일에 쓰기
		fwrite(buf, 1, read_cnt, fp);
	}
	
	printf("Complete!: %s \n", file_name);
	
	// 파일 수신 완료 메시지를 클라이언트에게 전송
	write(clnt_sd, "Thank you", 10);
	
	// 파일과 소켓 닫기
	fclose(fp);
	close(clnt_sd);
	return NULL;
}

// 에러 메시지를 출력하고 프로그램을 종료하는 함수
void error_handling(char *message)
{
	fputs(message, stderr);    // 표준 에러 출력에 메시지 출력
	fputc('\n', stderr);       // 줄바꿈 출력
	exit(1);                   // 프로그램 종료 (에러 코드 1)
}
