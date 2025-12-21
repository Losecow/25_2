#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define CHUNK 4096

typedef struct
{
    // Socket descriptor
    int sd;
    // Server address
    char *server_ip;
    // Server port
    int server_port;

    char client_id[64];
    char filename[256];

    FILE *fp;
    long file_size;
    long offset;
} UploadClient;

// 서버에 접속하는 함수
int connect_server(UploadClient *uc)
{
    // Socket descriptor 생성
    int sd = socket(PF_INET, SOCK_STREAM, 0);

    // 서버 주소 설정
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(uc->server_ip);
    serv.sin_port = htons(uc->server_port);

    // 서버에 접속 시도
    printf("서버 접속 시도: %s:%d\n", uc->server_ip, uc->server_port);
    fflush(stdout);

    // 서버에 접속
    if (connect(sd, (struct sockaddr *)&serv, sizeof(serv)) < 0)
    {
        perror("connect");
        return -1;
    }

    // 접속 성공 메시지 출력
    printf("서버 접속 성공: %s:%d\n", uc->server_ip, uc->server_port);
    fflush(stdout);

    // Socket descriptor 저장
    uc->sd = sd;
    return 0;
}

// FIRST 메시지 전송 함수
int send_FIRST(UploadClient *uc)
{
    // FIRST 메시지 생성
    char msg[256];
    snprintf(msg, sizeof(msg), "FIRST %s %s %ld\n",
             uc->client_id, uc->filename, uc->file_size);

    // msg_len: 메시지의 길이
    // sent: 이미 전송된 바이트 수
    // send_cnt: send 함수의 반환값 (전송된 바이트 수)
    int msg_len = strlen(msg);
    int sent = 0;
    int send_cnt;

    // 아직 전송되지 않은 메시지 부분이 남아있는 동안 반복
    while (sent < msg_len)
    {
        // send 함수로 메시지 전송
        send_cnt = send(uc->sd, msg + sent, msg_len - sent, 0);

        // 반환값이 0 이하이면 오류 처리
        if (send_cnt <= 0)
            return -1;

        // 전송된 바이트 수 누적
        sent += send_cnt;
    }

    // 서버로부터 ACK 응답 수신

    // 응답 메시지 저장
    char line[128];

    // 현재 위치
    int pos = 0;

    // 읽은 바이트 수
    int read_len;

    // 한 글자씩 읽기 위한 변수
    char c;

    // 한 글자씩 읽어서 응답 메시지 생성
    while ((read_len = read(uc->sd, &c, 1)) > 0)
    {
        // 읽은 글자를 응답 메시지에 추가
        line[pos++] = c;

        // 개행 문자이거나 버퍼가 가득 찼으면 종료
        if (c == '\n' || pos >= (int)sizeof(line) - 1)
            break;
    }

    // 읽기 오류 처리
    if (read_len <= 0)
        return -1;

    // 응답 메시지 종료 문자 추가
    line[pos] = '\0';

    // ACK 메시지에서 offset 추출
    sscanf(line, "ACK %ld", &uc->offset);

    // 파일 포인터를 offset 위치로 이동
    fseek(uc->fp, uc->offset, SEEK_SET);

    return 0;
}

// RESUME 메시지 전송 함수
int send_RESUME(UploadClient *uc)
{
    // RESUME 메시지 생성
    char msg[256];
    snprintf(msg, sizeof(msg), "RESUME %s %s\n", uc->client_id, uc->filename);

    // msg_len: 메시지의 길이
    // sent: 이미 전송된 바이트 수
    // send_cnt: send 함수의 반환값 (전송된 바이트 수)

    int msg_len = strlen(msg);
    int sent = 0;
    int send_cnt;

    // 아직 전송되지 않은 메시지 부분이 남아있는 동안 반복
    while (sent < msg_len)
    {
        // send 함수로 메시지 전송
        send_cnt = send(uc->sd, msg + sent, msg_len - sent, 0);

        // 반환값이 0 이하이면 return -1
        if (send_cnt <= 0)
            return -1;
        // 전송된 바이트 수 누적
        sent += send_cnt;
    }

    // 서버로부터 ACK 응답 수신
    char line[128];
    int pos = 0;
    int read_len;
    char c;

    // 한 글자씩 읽어서 응답 메시지 생성하는 반복문
    while ((read_len = read(uc->sd, &c, 1)) > 0)
    {
        line[pos++] = c;
        if (c == '\n' || pos >= (int)sizeof(line) - 1)
            break;
    }

    // read_len이 0 이하인 경우 return -1
    if (read_len <= 0)
        return -1;

    // 응답 메시지 종료 문자 추가
    line[pos] = '\0';

    // ACK 메시지에서 offset 추출
    sscanf(line, "ACK %ld", &uc->offset);
    // 파일 포인터를 offset 위치로 이동
    fseek(uc->fp, uc->offset, SEEK_SET);

    return 0;
}

// DATA 청크 전송 함수
int send_DATA_chunk(UploadClient *uc, char *buf, int size)
{
    // DATA 헤더 생성
    char header[64];
    sprintf(header, "DATA %d\n", size);

    // header_len: 헤더의 길이
    // sent: 이미 전송된 바이트 수
    // send_cnt: send 함수의 반환값 (전송된 바이트 수)

    int header_len = strlen(header);
    int sent = 0;
    int send_cnt;

    while (sent < header_len)
    {
        send_cnt = send(uc->sd, header + sent, header_len - sent, 0);
        if (send_cnt <= 0)
            return -1;
        sent += send_cnt;
    }

    // 데이터 청크 전송
    sent = 0;
    while (sent < size)
    {
        send_cnt = send(uc->sd, buf + sent, size - sent, 0);
        if (send_cnt <= 0)
            return -1;
        sent += send_cnt;
    }

    // 서버로부터 ACK 응답 수신
    char line[128];
    int pos = 0;
    int read_len;
    char c;

    // 한 글자씩 읽어서 응답 메시지 생성
    while ((read_len = read(uc->sd, &c, 1)) > 0)
    {
        line[pos++] = c;
        if (c == '\n' || pos >= (int)sizeof(line) - 1)
            break;
    }

    if (read_len <= 0)
        return -1;

    line[pos] = '\0';

    // ACK 메시지에서 offset 추출
    sscanf(line, "ACK %ld", &uc->offset);

    return 0;
}

// FIN 메시지 전송 함수
int send_FIN(UploadClient *uc)
{
    // FIN 메시지 생성

    const char *fin_msg = "FIN\n";
    int msg_len = strlen(fin_msg);
    int sent = 0;
    int send_cnt;

    // FIN 메시지 전송
    while (sent < msg_len)
    {
        send_cnt = send(uc->sd, fin_msg + sent, msg_len - sent, 0);
        if (send_cnt <= 0)
            return -1;
        sent += send_cnt;
    }

    // 서버로부터 COMPLETE 응답 수신
    char line[128];
    int pos = 0;
    int read_len;
    char c;

    // 한 글자씩 읽어서 응답 메시지 생성
    while ((read_len = read(uc->sd, &c, 1)) > 0)
    {
        // 읽은 글자를 응답 메시지에 추가
        line[pos++] = c;
        // 개행 문자이거나 버퍼가 가득 찼으면 종료
        if (c == '\n' || pos >= (int)sizeof(line) - 1)
            break;
    }

    if (read_len <= 0)
        return -1;

    line[pos] = '\0';

    // 서버가 COMPLETE 메시지를 보낼 때까지 반복
    if (strncmp(line, "COMPLETE", 8) == 0)
        return 0;

    return -1;
}

// 파일 업로드 함수
int upload_file(UploadClient *uc)
{
    // 파일에서 읽은 데이터 청크를 저장하는 버퍼
    char buf[CHUNK];

    // 파일의 끝까지 반복
    while (uc->offset < uc->file_size)
    {
        // 파일에서 데이터 청크 읽기
        fseek(uc->fp, uc->offset, SEEK_SET);
        int n = fread(buf, 1, CHUNK, uc->fp);

        // 읽은 바이트 수가 0 이하이면 종료
        if (n <= 0)
            break;

        // 데이터 청크 전송
        if (send_DATA_chunk(uc, buf, n) == 0)
            continue;

        // send_DATA_chunk 실패 시 재접속 및 RESUME 전송
        printf("[send-실패---재접속-요청]\n");

        // 기존 소켓 닫기
        close(uc->sd);

        // 서버에 재접속 시도
        while (connect_server(uc) < 0)
        {
            // 접속 실패 시 1초 대기 후 재시도
            perror("connect");
            // 1초 대기
            sleep(1);
        }

        // 재접속 성공 메세지
        printf("재접속 성공: %s:%d\n", uc->server_ip, uc->server_port);

        // RESUME 메시지 전송
        if (send_RESUME(uc) < 0)
            return -1;
        printf("RESUME -- offset = %ld\n", uc->offset);
    }

    // FIN 메시지 전송
    return send_FIN(uc);
}

// 메인 함수
int main(int argc, char *argv[])
{
    // 인자 개수 확인
    if (argc != 5)
    {
        printf("Usage: %s <IP> <port> <ClientID> <File>\n", argv[0]);
        exit(1);
    }

    // UploadClient 구조체 초기화
    UploadClient uc;
    uc.server_ip = argv[1];
    uc.server_port = atoi(argv[2]);
    strcpy(uc.client_id, argv[3]);
    strcpy(uc.filename, argv[4]);

    // 파일 열기
    uc.fp = fopen(uc.filename, "rb");
    if (!uc.fp)
    {
        printf("파일을 열 수 없음: %s\n", uc.filename);
        exit(1);
    }

    // 파일 크기 구하기
    fseek(uc.fp, 0, SEEK_END);
    uc.file_size = ftell(uc.fp);
    fseek(uc.fp, 0, SEEK_SET);

    // 서버에 접속 시도
    while (connect_server(&uc) < 0)
        sleep(1);

    // FIRST 메시지 전송
    if (send_FIRST(&uc) < 0)
    {
        printf("FIRST 실패\n");
        return -1;
    }

    // 파일 업로드 시작
    upload_file(&uc);

    // 파일 및 소켓 닫기
    fclose(uc.fp);
    close(uc.sd);
    return 0;
}
