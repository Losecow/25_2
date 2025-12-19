/************************************************************
 * CLIENT: Stateful File Upload with Resume
 *
 * 파일을 청크 단위로 서버에 업로드하며, 연결이 끊어져도
 * 재연결 후 이어서 업로드할 수 있는 기능을 제공합니다.
 *
 * 출처 요약:
 * - 기본 TCP 클라이언트 골격:
 *     컴네코드/practice01/tcp_client.c 11-34라인
 * - 파일 업로드 루프/파일 처리:
 *     컴네코드/practice03/file_up_client.c 12-35라인,
 *     컴네코드/practice07/file_up_client.c 42-57라인
 * - 부분 send 처리 패턴:
 *     컴네코드/practice06/echo_client.c 47-54라인
 * - 1바이트씩 읽기 패턴:
 *     컴네코드/practice01/tcp_client.c 36-44라인
 * - 파일 크기 계산(fseek/ftell):
 *     컴네코드/practice08/web_server.c 117-120라인
 ************************************************************/
// 출처: 컴네코드/practice01/tcp_client.c 1-7라인, 컴네코드/practice03/file_up_client.c 1-6라인
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

// 출처: 컴네코드/practice07/file_up_client.c 9라인(BUF_SIZE 정의)을 4096으로 변경하여 사용
#define CHUNK 4096 // 한 번에 전송할 데이터 청크 크기 (4KB)

/* ------------------- 클라이언트 상태 ------------------- */

/**
 * UploadClient: 클라이언트의 업로드 상태를 저장하는 구조체
 */
// 출처: 과제 요구사항 + 컴네코드/practice03/file_up_client.c 23-27라인의 파일/버퍼 관리 방식 참고
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

/* ------------------- 서버 연결 ------------------- */

/**
 * connect_server: 서버에 TCP 연결을 시도
 *
 * @param uc: UploadClient 구조체 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
// 출처: 컴네코드/practice01/tcp_client.c 24-34라인(socket, connect 부분)을 함수로 분리
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

/* ------------------- 메시지 핸들러 ------------------- */

/**
 * send_FIRST: 첫 업로드 시작 메시지 전송
 *
 * 프로토콜: "FIRST <client_id> <filename> <file_size>\n"
 * 응답: "ACK <offset>\n"
 *
 * @param uc: UploadClient 구조체 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
// 출처:
// - 메세지 전송(부분 전송 while 루프):
//     컴네코드/practice06/echo_client.c 47-54라인 (recv_len/recv_cnt 패턴을 send로 변형)
// - ACK 읽기(1바이트씩 read):
//     컴네코드/practice01/tcp_client.c 36-44라인 패턴을 사용해 '\n'까지 읽도록 확장
int send_FIRST(UploadClient *uc)
{
    char msg[256];
    snprintf(msg, sizeof(msg), "FIRST %s %s %ld\n",
             uc->client_id, uc->filename, uc->file_size);

    // practice06 패턴: 부분 전송 처리
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

    // practice01 패턴: read()로 1바이트씩 읽기 (tcp_client.c 36-44라인 변형)
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

/**
 * send_RESUME: 재연결 후 업로드 재개 메시지 전송
 *
 * 프로토콜: "RESUME <client_id> <filename>\n"
 * 응답: "ACK <offset>\n"
 *
 * @param uc: UploadClient 구조체 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
// 출처:
// - send 패턴: 컴네코드/practice06/echo_client.c 47-54라인 변형
// - ACK 수신: 컴네코드/practice01/tcp_client.c 36-44라인 변형
int send_RESUME(UploadClient *uc)
{
    char msg[256];
    snprintf(msg, sizeof(msg), "RESUME %s %s\n", uc->client_id, uc->filename);

    // practice06 패턴: 부분 전송 처리
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

    // practice01 패턴: read()로 1바이트씩 읽기 (tcp_client.c 36-44라인 변형)
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

/**
 * send_DATA_chunk: 데이터 청크 전송
 *
 * 프로토콜: "DATA <chunk_size>\n" + 실제 데이터 바이너리
 * 응답: "ACK <new_offset>\n"
 *
 * @param uc: UploadClient 구조체 포인터
 * @param buf: 전송할 데이터 버퍼
 * @param size: 전송할 데이터 크기
 * @return: 성공 시 0, 실패 시 -1
 */
// 출처:
// - 헤더/데이터 전송(부분 전송 while 루프):
//     컴네코드/practice06/echo_client.c 47-54라인 패턴을 header, data에 각각 적용
// - ACK 수신(1바이트 read 루프):
//     컴네코드/practice01/tcp_client.c 36-44라인 패턴 변형
int send_DATA_chunk(UploadClient *uc, char *buf, int size)
{
    char header[64];
    sprintf(header, "DATA %d\n", size);

    // practice06 패턴: 헤더 부분 전송 처리
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

    // practice06 패턴: 데이터 부분 전송 처리
    sent = 0;
    while (sent < size)
    {
        send_cnt = send(uc->sd, buf + sent, size - sent, 0);
        if (send_cnt <= 0)
            return -1;
        sent += send_cnt;
    }

    // practice01 패턴: read()로 1바이트씩 읽기 (tcp_client.c 36-44라인 변형)
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

/**
 * send_FIN: 업로드 완료 메시지 전송
 *
 * 프로토콜: "FIN\n"
 * 응답: "COMPLETE\n"
 *
 * @param uc: UploadClient 구조체 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
// 출처:
// - FIN 전송(부분 전송 while 루프):
//     컴네코드/practice06/echo_client.c 47-54라인 패턴
// - COMPLETE 수신(1바이트 read 루프):
//     컴네코드/practice01/tcp_client.c 36-44라인 패턴 변형
int send_FIN(UploadClient *uc)
{
    // practice06 패턴: 부분 전송 처리
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

    // practice01 패턴: read()로 1바이트씩 읽기 (tcp_client.c 36-44라인 변형)
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

/* ------------------- 업로드 로직 ------------------- */

/**
 * upload_file: 파일을 청크 단위로 업로드하는 메인 로직
 *
 * @param uc: UploadClient 구조체 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
// 출처:
// - 기본 파일 읽기/전송 루프:
//     컴네코드/practice07/file_up_client.c 42-57라인
// - 재접속 + RESUME 프로토콜 로직:
//     과제용으로 추가 구현 (practice 코드 없음)
int upload_file(UploadClient *uc)
{
    char buf[CHUNK];

    // practice07 패턴: 파일 읽기 루프
    while (uc->offset < uc->file_size)
    {
        fseek(uc->fp, uc->offset, SEEK_SET);
        int n = fread(buf, 1, CHUNK, uc->fp);
        if (n <= 0)
            break;

        if (send_DATA_chunk(uc, buf, n) == 0)
            continue;

        // 전송 실패 시 재연결
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

/* ------------------- 클라이언트 메인 ------------------- */

/**
 * main: 클라이언트 프로그램의 진입점
 *
 * 사용법: ./client <IP> <port> <ClientID> <File>
 */
// 출처:
// - 인자 처리 및 TCP 클라이언트 골격:
//     컴네코드/practice01/tcp_client.c 11-34라인
// - 파일 열기 및 이름 처리:
//     컴네코드/practice03/file_up_client.c 12-35라인
// - 파일 크기 계산(fseek/ftell):
//     컴네코드/practice08/web_server.c 117-120라인
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

    // practice08 패턴: 파일 크기 계산
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
