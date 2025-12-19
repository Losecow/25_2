#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define CHUNK 4096

typedef struct
{
    int sd;
    char *server_ip;
    int server_port;

    char client_id[64];
    char filename[256];

    FILE *fp;
    long file_size;
    long offset;
} UploadClient;

// 서버에 TCP 연결을 맺는 함수
int connect_server(UploadClient *uc)
{
    int sd = socket(PF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(uc->server_ip);
    serv.sin_port = htons(uc->server_port);

    printf("서버 접속 시도: %s:%d\n", uc->server_ip, uc->server_port);
    fflush(stdout);
    if (connect(sd, (struct sockaddr *)&serv, sizeof(serv)) < 0)
    {
        perror("connect");
        return -1;
    }
    printf("서버 접속 성공: %s:%d\n", uc->server_ip, uc->server_port);
    fflush(stdout);

    uc->sd = sd;
    return 0;
}

// FIRST 메시지를 보내고 서버의 ACK로 시작 offset을 받는 함수
int send_FIRST(UploadClient *uc)
{
    char msg[256];
    snprintf(msg, sizeof(msg), "FIRST %s %s %ld\n",
             uc->client_id, uc->filename, uc->file_size);

    int msg_len = strlen(msg);
    int sent = 0;
    int send_cnt;
    while (sent < msg_len)
    {
        send_cnt = send(uc->sd, msg + sent, msg_len - sent, 0);
        if (send_cnt <= 0)
            return -1;
        sent += send_cnt;
    }

    char line[128];
    int pos = 0;
    int read_len;
    char c;

    while ((read_len = read(uc->sd, &c, 1)) > 0)
    {
        line[pos++] = c;
        if (c == '\n' || pos >= (int)sizeof(line) - 1)
            break;
    }

    if (read_len <= 0)
        return -1;

    line[pos] = '\0';

    sscanf(line, "ACK %ld", &uc->offset);
    fseek(uc->fp, uc->offset, SEEK_SET);

    return 0;
}

// 재접속 후 RESUME 메시지를 보내고 서버의 ACK로 offset을 다시 받는 함수
int send_RESUME(UploadClient *uc)
{
    char msg[256];
    snprintf(msg, sizeof(msg), "RESUME %s %s\n", uc->client_id, uc->filename);

    int msg_len = strlen(msg);
    int sent = 0;
    int send_cnt;
    while (sent < msg_len)
    {
        send_cnt = send(uc->sd, msg + sent, msg_len - sent, 0);
        if (send_cnt <= 0)
            return -1;
        sent += send_cnt;
    }

    char line[128];
    int pos = 0;
    int read_len;
    char c;

    while ((read_len = read(uc->sd, &c, 1)) > 0)
    {
        line[pos++] = c;
        if (c == '\n' || pos >= (int)sizeof(line) - 1)
            break;
    }

    if (read_len <= 0)
        return -1;

    line[pos] = '\0';

    sscanf(line, "ACK %ld", &uc->offset);
    fseek(uc->fp, uc->offset, SEEK_SET);

    return 0;
}

// DATA 헤더와 파일 청크 데이터를 서버로 보내고 ACK로 offset을 갱신하는 함수
int send_DATA_chunk(UploadClient *uc, char *buf, int size)
{
    char header[64];
    sprintf(header, "DATA %d\n", size);

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

    sent = 0;
    while (sent < size)
    {
        send_cnt = send(uc->sd, buf + sent, size - sent, 0);
        if (send_cnt <= 0)
            return -1;
        sent += send_cnt;
    }

    char line[128];
    int pos = 0;
    int read_len;
    char c;

    while ((read_len = read(uc->sd, &c, 1)) > 0)
    {
        line[pos++] = c;
        if (c == '\n' || pos >= (int)sizeof(line) - 1)
            break;
    }

    if (read_len <= 0)
        return -1;

    line[pos] = '\0';

    sscanf(line, "ACK %ld", &uc->offset);

    return 0;
}

// 업로드가 끝났음을 알리는 FIN 메시지를 보내고 COMPLETE 응답을 확인하는 함수
int send_FIN(UploadClient *uc)
{
    const char *fin_msg = "FIN\n";
    int msg_len = strlen(fin_msg);
    int sent = 0;
    int send_cnt;
    while (sent < msg_len)
    {
        send_cnt = send(uc->sd, fin_msg + sent, msg_len - sent, 0);
        if (send_cnt <= 0)
            return -1;
        sent += send_cnt;
    }

    char line[128];
    int pos = 0;
    int read_len;
    char c;

    while ((read_len = read(uc->sd, &c, 1)) > 0)
    {
        line[pos++] = c;
        if (c == '\n' || pos >= (int)sizeof(line) - 1)
            break;
    }

    if (read_len <= 0)
        return -1;

    line[pos] = '\0';

    if (strncmp(line, "COMPLETE", 8) == 0)
        return 0;

    return -1;
}

// 파일 전체를 청크 단위로 전송하고, 실패 시 재접속/RESUME까지 처리하는 메인 업로드 함수
int upload_file(UploadClient *uc)
{
    char buf[CHUNK];

    while (uc->offset < uc->file_size)
    {
        fseek(uc->fp, uc->offset, SEEK_SET);
        int n = fread(buf, 1, CHUNK, uc->fp);
        if (n <= 0)
            break;

        if (send_DATA_chunk(uc, buf, n) == 0)
            continue;

        printf("⚠️ send 실패 → 재접속 시도 중...\n");
        close(uc->sd);

        while (connect_server(uc) < 0)
        {
            perror("connect");
            sleep(1);
        }
        printf("재접속 성공: %s:%d\n", uc->server_ip, uc->server_port);

        if (send_RESUME(uc) < 0)
            return -1;
        printf("RESUME 전송 성공, offset=%ld\n", uc->offset);
    }

    return send_FIN(uc);
}

// 프로그램 진입점: 인자 파싱, 파일 열기, 서버 연결, 업로드 전체 흐름을 제어
int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        printf("Usage: %s <IP> <port> <ClientID> <File>\n", argv[0]);
        exit(1);
    }

    UploadClient uc;
    uc.server_ip = argv[1];
    uc.server_port = atoi(argv[2]);
    strcpy(uc.client_id, argv[3]);
    strcpy(uc.filename, argv[4]);

    uc.fp = fopen(uc.filename, "rb");
    if (!uc.fp)
    {
        printf("파일을 열 수 없음: %s\n", uc.filename);
        exit(1);
    }

    fseek(uc.fp, 0, SEEK_END);
    uc.file_size = ftell(uc.fp);
    fseek(uc.fp, 0, SEEK_SET);

    while (connect_server(&uc) < 0)
        sleep(1);

    if (send_FIRST(&uc) < 0)
    {
        printf("FIRST 실패\n");
        return -1;
    }

    upload_file(&uc);

    fclose(uc.fp);
    close(uc.sd);
    return 0;
}


