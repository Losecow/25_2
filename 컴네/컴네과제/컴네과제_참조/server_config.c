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
    int sd;
    char client_id[64];
    char filename[256];
    char filepath[512];

    FILE *fp;
    long stored_offset;
    long expected_size;
} UploadSession;

// 현재까지 저장된 파일 위치(offset)를 클라이언트에게 ACK로 알려주는 함수
void send_ACK(int sd, long offset)
{
    char msg[64];
    int len = sprintf(msg, "ACK %ld\n", offset);

    int sent = 0;
    int write_cnt;
    while (sent < len)
    {
        write_cnt = write(sd, msg + sent, len - sent);
        if (write_cnt <= 0)
            return;
        sent += write_cnt;
    }
}

// 업로드가 정상적으로 끝났음을 클라이언트에게 COMPLETE로 알려주는 함수
void send_COMPLETE(int sd)
{
    const char *msg = "COMPLETE\n";
    int len = strlen(msg);

    int sent = 0;
    int write_cnt;
    while (sent < len)
    {
        write_cnt = write(sd, msg + sent, len - sent);
        if (write_cnt <= 0)
            return;
        sent += write_cnt;
    }
}

// FIRST 메시지를 처리하고, 파일 경로/크기/offset을 초기화한 뒤 ACK를 보내는 함수
int handle_FIRST(UploadSession *s, char *id, char *file, long filesize)
{
    strcpy(s->client_id, id);
    strcpy(s->filename, file);
    s->expected_size = filesize;

    mkdir(id, 0777);
    sprintf(s->filepath, "./%s/%s", id, file);

    FILE *f = fopen(s->filepath, "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        s->stored_offset = ftell(f);
        fclose(f);
    }
    else
    {
        s->stored_offset = 0;
    }

    s->fp = fopen(s->filepath, "ab");
    send_ACK(s->sd, s->stored_offset);

    return 0;
}

// RESUME 메시지를 처리하고, 기존 파일 크기를 기준으로 offset을 잡아 ACK를 보내는 함수
int handle_RESUME(UploadSession *s, char *id, char *file)
{
    strcpy(s->client_id, id);
    strcpy(s->filename, file);

    sprintf(s->filepath, "./%s/%s", id, file);

    FILE *f = fopen(s->filepath, "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        s->stored_offset = ftell(f);
        fclose(f);
    }
    else
    {
        s->stored_offset = 0;
    }

    s->fp = fopen(s->filepath, "ab");
    send_ACK(s->sd, s->stored_offset);

    return 0;
}

// DATA 메시지로 넘어온 크기만큼 본문 데이터를 받아 파일에 쓰고, 새로운 offset을 ACK로 보내는 함수
int handle_DATA(UploadSession *s, int chunkSize)
{
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

    fwrite(buf, 1, chunkSize, s->fp);
    fflush(s->fp);

    s->stored_offset += chunkSize;
    send_ACK(s->sd, s->stored_offset);

    free(buf);
    return 0;
}

// FIN 메시지를 처리하고, 파일을 닫은 뒤 COMPLETE를 전송하는 함수
int handle_FIN(UploadSession *s)
{
    fclose(s->fp);
    send_COMPLETE(s->sd);
    return 0;
}

// 각 클라이언트와 1:1로 통신하며 FIRST/RESUME/DATA/FIN 프로토콜을 처리하는 스레드 함수
void *handle_client(void *arg)
{
    int sd = *(int *)arg;
    free(arg);

    UploadSession S;
    memset(&S, 0, sizeof(S));
    S.sd = sd;

    char line[512];
    int read_len;
    char c;

    while (1)
    {
        int pos = 0;
        while ((read_len = read(sd, &c, 1)) > 0)
        {
            line[pos++] = c;
            if (c == '\n' || pos >= (int)sizeof(line) - 1)
                break;
        }

        if (read_len <= 0 && pos == 0)
            break;

        line[pos] = '\0';

        if (strncmp(line, "FIRST", 5) == 0)
        {
            char id[64], file[256];
            long size;
            sscanf(line, "FIRST %s %s %ld", id, file, &size);
            handle_FIRST(&S, id, file, size);
            printf("[FIRST] id=%s file=%s size=%ld offset=%ld\n",
                   id, file, size, S.stored_offset);
        }
        else if (strncmp(line, "RESUME", 6) == 0)
        {
            char id[64], file[256];
            sscanf(line, "RESUME %s %s", id, file);
            handle_RESUME(&S, id, file);
            printf("[RESUME] id=%s file=%s offset=%ld\n",
                   id, file, S.stored_offset);
        }
        else if (strncmp(line, "DATA", 4) == 0)
        {
            int chunk;
            sscanf(line, "DATA %d", &chunk);
            if (handle_DATA(&S, chunk) < 0)
                break;
            printf("[DATA ] chunk=%d -> offset=%ld\n", chunk, S.stored_offset);
        }
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

// 서버 진입점: 소켓 생성/바인딩/리스닝 후, 접속마다 스레드를 만들어 업로드를 처리
int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int serv_sd = socket(PF_INET, SOCK_STREAM, 0);

    int option = 1;
    if (setsockopt(serv_sd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
    {
        perror("setsockopt");
        exit(1);
    }

    struct sockaddr_in serv, clnt;
    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(atoi(argv[1]));

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

    printf("Server started on port %s\n", argv[1]);

    while (1)
    {
        socklen_t sz = sizeof(clnt);
        int clnt_sd = accept(serv_sd, (struct sockaddr *)&clnt, &sz);
        if (clnt_sd < 0)
        {
            perror("accept");
            continue;
        }

        int *pclient = malloc(sizeof(int));
        if (!pclient)
        {
            close(clnt_sd);
            continue;
        }
        *pclient = clnt_sd;

        pthread_t t;
        pthread_create(&t, NULL, handle_client, pclient);
        pthread_detach(t);

        printf("Connected: %s\n", inet_ntoa(clnt.sin_addr));
    }

    close(serv_sd);
    return 0;
}


