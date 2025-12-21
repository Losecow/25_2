#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>

#define BUF_SIZE 4096

typedef struct
{
    // Socket descriptor
    int sd;
    // 클라이언트 ID와 파일 정보
    char client_id[64];
    char filename[256];
    char filepath[512];

    FILE *fp;
    long stored_offset;
    long expected_size;
} UploadSession;

// 클라이언트에게 ACK 메시지를 전송하는 함수
void send_ACK(int sd, long offset)
{
    char msg[64];
    int len = sprintf(msg, "ACK %ld\n", offset);

    int sent = 0;
    int write_cnt;

    // 전체 메시지가 전송될 때까지 반복
    while (sent < len)
    {
        // 부분적으로 메시지 전송 (부분 = write_cnt = 실제로 전송된 바이트 수)
        write_cnt = write(sd, msg + sent, len - sent);

        // 조건: 전송 실패 시 함수 종료
        if (write_cnt <= 0)
            return;

        // 전송된 바이트 수 누적
        sent += write_cnt;
    }
}

// 클라이언트에게 COMPLETE 메시지를 전송하는 함수
void send_COMPLETE(int sd)
{
    const char *msg = "COMPLETE\n";
    int len = strlen(msg);

    int sent = 0;
    int write_cnt;

    // 전체 메시지가 전송될 때까지 반복
    while (sent < len)
    {
        write_cnt = write(sd, msg + sent, len - sent);
        if (write_cnt <= 0)
            return;
        sent += write_cnt;
    }
}

// FIRST 명령 처리 함수 - 클라이언트 ID, 파일 이름, 파일 크기를 받아 세션 초기화
int handle_FIRST(UploadSession *s, char *id, char *file, long filesize)
{
    // 세션 정보 설정
    strcpy(s->client_id, id);
    strcpy(s->filename, file);

    // 예상 파일 크기 설정
    s->expected_size = filesize;

    // 클라이언트 ID에 해당하는 디렉토리 생성
    mkdir(id, 0777);
    sprintf(s->filepath, "./%s/%s", id, file);

    FILE *f = fopen(s->filepath, "rb");

    // 기존 파일이 있을 경우 오프셋 계산
    // 파일이 존재하면 끝으로 이동하여 크기 측정
    if (f)
    {
        fseek(f, 0, SEEK_END);
        s->stored_offset = ftell(f);
        fclose(f);
    }

    // 파일이 없으면 오프셋 0으로 설정
    else
    {
        s->stored_offset = 0;
    }

    // 파일을 이어쓰기 모드로 열기
    s->fp = fopen(s->filepath, "ab");
    // 현재 오프셋을 클라이언트에게 전송
    send_ACK(s->sd, s->stored_offset);

    return 0;
}

// RESUME 명령 처리 함수 - 클라이언트 ID와 파일 이름을 받아 세션 복원
int handle_RESUME(UploadSession *s, char *id, char *file)
{
    // 세션 정보 설정
    strcpy(s->client_id, id);
    strcpy(s->filename, file);

    sprintf(s->filepath, "./%s/%s", id, file);

    // 기존 파일이 있을 경우 오프셋 계산
    // 파일이 존재하면 끝으로 이동하여 크기 측정
    FILE *f = fopen(s->filepath, "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        s->stored_offset = ftell(f);
        fclose(f);
    }

    // 파일이 없으면 오프셋 0으로 설정
    else
    {
        s->stored_offset = 0;
    }

    // 파일을 이어쓰기 모드로 열기
    s->fp = fopen(s->filepath, "ab");
    // 현재 오프셋을 클라이언트에게 전송
    send_ACK(s->sd, s->stored_offset);

    return 0;
}

// DATA 명령 처리 함수 - 정해진 크기만큼만 데이터를 수신해서 파일에 저장
int handle_DATA(UploadSession *s, int chunkSize)
{
    // 1. chunkSize 크기만큼 데이터를 소켓에서 읽음
    char *buf = malloc(chunkSize);
    if (!buf)
        return -1;

    int received = 0;
    while (received < chunkSize)
    {
        int n = read(s->sd, buf + received, chunkSize - received);
        if (n <= 0)
        {
            free(buf);
            return -1;
        }
        received += n;
    }

    // 2. 읽은 데이터를 파일에 저장
    fwrite(buf, 1, chunkSize, s->fp);
    fflush(s->fp);

    // 3. stored_offset 업데이트
    s->stored_offset += chunkSize;
    // 4. 업데이트된 stored_offset을 클라이언트에게 ACK로 전송
    send_ACK(s->sd, s->stored_offset);

    // 5. 메모리 free
    free(buf);
    return 0;
}

// FIN 명령 처리 함수 - 업로드 완료 처리
int handle_FIN(UploadSession *s)
{
    fclose(s->fp);
    send_COMPLETE(s->sd);
    return 0;
}

// 클라이언트 연결 처리 스레드 함수
void *handle_client(void *arg)
{
    // 소켓 디스크립터
    int sd = *(int *)arg;
    free(arg);

    // 업로드 세션 구조체 초기화
    UploadSession S;
    memset(&S, 0, sizeof(S));
    S.sd = sd;

    // 명령어 수신 버퍼
    char line[512];
    int read_len;
    char c;

    // 명령어 처리 루프
    while (1)
    {
        // 한 줄씩 명령어 읽기
        int pos = 0;
        while ((read_len = read(sd, &c, 1)) > 0)
        {
            // 개행 문자 또는 버퍼 크기 초과 시 중단
            line[pos++] = c;
            if (c == '\n' || pos >= (int)sizeof(line) - 1)
                break;
        }

        // 연결 종료 또는 오류 시 루프 탈출
        if (read_len <= 0 && pos == 0)
            break;

        // 문자열 종료 문자 추가
        line[pos] = '\0';

        // 명령어 파싱 및 처리
        if (strncmp(line, "FIRST", 5) == 0)
        {
            char id[64], file[256];
            long size;
            sscanf(line, "FIRST %s %s %ld", id, file, &size);
            handle_FIRST(&S, id, file, size);
            printf("[FIRST] id=%s file=%s size=%ld offset=%ld\n",
                   id, file, size, S.stored_offset);
        }

        // RESUME 명령 처리
        else if (strncmp(line, "RESUME", 6) == 0)
        {
            char id[64], file[256];
            sscanf(line, "RESUME %s %s", id, file);
            handle_RESUME(&S, id, file);
            printf("[RESUME] id=%s file=%s offset=%ld\n",
                   id, file, S.stored_offset);
        }

        // DATA 명령 처리
        else if (strncmp(line, "DATA", 4) == 0)
        {
            int chunk;
            sscanf(line, "DATA %d", &chunk);
            if (handle_DATA(&S, chunk) < 0)
                break;
            printf("[DATA ] chunk=%d -> offset=%ld\n", chunk, S.stored_offset);
        }

        // FIN 명령 처리
        else if (strncmp(line, "FIN", 3) == 0)
        {
            handle_FIN(&S);
            printf("[FIN  ] completed id=%s file=%s size=%ld\n",
                   S.client_id, S.filename, S.stored_offset);
            break;
        }
    }
    close(sd);
    return NULL;
}

// 메인 함수 - 서버 소켓 설정 및 클라이언트 연결 대기
int main(int argc, char *argv[])
{
    // 포트 번호
    if (argc != 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // 서버 소켓 생성
    int serv_sd = socket(PF_INET, SOCK_STREAM, 0);

    // 소켓 옵션 설정: 주소 재사용
    int option = 1;
    if (setsockopt(serv_sd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
    {
        perror("setsockopt");
        exit(1);
    }

    // 서버 주소 구조체 설정
    struct sockaddr_in serv, clnt;
    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(atoi(argv[1]));

    // 바인드 / 리슨
    if (bind(serv_sd, (struct sockaddr *)&serv, sizeof(serv)) < 0)
    {
        perror("bind");
        exit(1);
    }
    if (listen(serv_sd, 10) < 0)
    {
        perror("listen");
        exit(1);
    }

    printf("Server start port: %s\n", argv[1]);

    // 클라이언트 연결 대기 및 처리
    while (1)
    {
        socklen_t sz = sizeof(clnt);

        // 클라이언트 연결 수락
        int clnt_sd = accept(serv_sd, (struct sockaddr *)&clnt, &sz);

        // 오류조건: accept 실패 시 계속 대기
        if (clnt_sd < 0)
        {
            perror("accept");
            continue;
        }

        // 클라이언트 처리 스레드 생성
        int *pclient = malloc(sizeof(int));
        if (!pclient)
        {
            close(clnt_sd);
            continue;
        }
        *pclient = clnt_sd;

        // 스레드 생성 및 분리
        pthread_t t;
        pthread_create(&t, NULL, handle_client, pclient);
        pthread_detach(t);

        // 연결된 클라이언트 정보 출력
        printf("Connected: %s\n", inet_ntoa(clnt.sin_addr));
    }

    // 서버 소켓 닫기
    close(serv_sd);
    return 0;
}
