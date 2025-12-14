/************************************************************
 * SERVER: Stateful File Upload with Resume Capability
 * 
 * 이 서버는 클라이언트로부터 파일을 수신하는 프로그램입니다.
 * 주요 기능:
 * - 멀티스레드로 여러 클라이언트 동시 처리
 * - 파일 업로드 재개 기능 (Resume)
 * - 클라이언트별로 디렉토리를 만들어 파일 저장
 * - 업로드 진행 상황 추적 및 동기화
 ************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>

#define BUF_SIZE 4096  // 버퍼 크기 (4KB)

/* ------------------- 공통 유틸 함수 ------------------- */

/**
 * safe_send: 데이터를 안전하게 전송하는 함수
 * send()는 한 번에 모든 데이터를 보내지 못할 수 있으므로,
 * 모든 데이터가 전송될 때까지 반복해서 전송합니다.
 * 
 * @param sd: 소켓 디스크립터
 * @param data: 전송할 데이터 버퍼
 * @param size: 전송할 데이터 크기
 * @return: 성공 시 전송한 바이트 수, 실패 시 -1
 */
int safe_send(int sd, const char* data, int size) {
    int sent = 0;
    while (sent < size) {
        int n = send(sd, data + sent, size - sent, 0);
        if (n <= 0) return -1;  // 전송 실패 또는 연결 종료
        sent += n;  // 전송된 바이트 수 누적
    }
    return sent;
}

/**
 * send_line: 문자열을 한 줄로 전송하는 함수
 * 
 * @param sd: 소켓 디스크립터
 * @param msg: 전송할 메시지 문자열
 * @return: safe_send의 반환값
 */
int send_line(int sd, const char* msg) {
    return safe_send(sd, msg, strlen(msg));
}

/**
 * recv_line: 소켓에서 한 줄을 수신하는 함수
 * '\n' 문자가 나올 때까지 또는 버퍼가 가득 찰 때까지 읽습니다.
 * 
 * @param sd: 소켓 디스크립터
 * @param buf: 데이터를 저장할 버퍼
 * @param maxlen: 버퍼의 최대 크기
 * @return: 수신한 바이트 수, 연결 종료 시 0, 오류 시 -1
 */
int recv_line(int sd, char* buf, int maxlen) {
    int pos = 0;
    char c;

    while (pos < maxlen - 1) {
        int n = recv(sd, &c, 1, 0);  // 한 바이트씩 읽기
        if (n == 0) return 0;  // 연결 종료
        if (n < 0) return -1;  // 오류 발생

        buf[pos++] = c;
        if (c == '\n') break;  // 줄바꿈 문자를 만나면 종료
    }
    buf[pos] = '\0';  // 문자열 종료 문자 추가
    return pos;
}

/**
 * recv_exact: 정확히 지정된 크기만큼 데이터를 수신하는 함수
 * recv()는 요청한 크기보다 적게 받을 수 있으므로,
 * 정확히 size 바이트를 받을 때까지 반복해서 수신합니다.
 * 
 * @param sd: 소켓 디스크립터
 * @param buf: 데이터를 저장할 버퍼
 * @param size: 수신할 데이터 크기
 * @return: 성공 시 수신한 바이트 수, 실패 시 -1
 */
int recv_exact(int sd, char* buf, int size) {
    int received = 0;
    while (received < size) {
        int n = recv(sd, buf + received, size - received, 0);
        if (n <= 0) return -1;  // 수신 실패 또는 연결 종료
        received += n;  // 수신된 바이트 수 누적
    }
    return received;
}
 
/* ------------------- 업로드 세션 ------------------- */

/**
 * UploadSession: 각 클라이언트의 업로드 세션 정보를 저장하는 구조체
 * 
 * - sd: 클라이언트와의 소켓 디스크립터
 * - client_id: 클라이언트 식별자
 * - filename: 업로드되는 파일명
 * - filepath: 서버에 저장될 파일의 전체 경로
 * - fp: 저장할 파일의 파일 포인터
 * - stored_offset: 현재까지 저장된 바이트 수
 * - expected_size: 예상되는 전체 파일 크기 (FIRST 메시지에서 받음)
 */
typedef struct {
    int sd;
    char client_id[64];
    char filename[256];
    char filepath[512];

    FILE* fp;
    long stored_offset;
    long expected_size;
} UploadSession;
 
/* ------------------- 메시지 핸들러 ------------------- */

/**
 * send_ACK: 클라이언트에게 현재 저장된 offset을 알리는 함수
 * 
 * @param sd: 소켓 디스크립터
 * @param offset: 현재까지 저장된 바이트 수
 */
void send_ACK(int sd, long offset) {
    char msg[64];
    sprintf(msg, "ACK %ld\n", offset);
    send_line(sd, msg);
}

/**
 * send_COMPLETE: 업로드 완료 메시지를 클라이언트에게 전송
 * 
 * @param sd: 소켓 디스크립터
 */
void send_COMPLETE(int sd) {
    send_line(sd, "COMPLETE\n");
}

/**
 * handle_FIRST: FIRST 메시지 처리
 * 클라이언트가 처음 업로드를 시작할 때 호출됩니다.
 * 
 * 동작:
 * 1. 클라이언트 ID로 디렉토리 생성
 * 2. 기존 파일이 있는지 확인하고, 있으면 현재 크기를 offset으로 설정
 * 3. 파일을 append 모드로 열어서 이어서 저장할 수 있게 함
 * 4. 현재 offset을 클라이언트에게 전송
 * 
 * 프로토콜: "FIRST <client_id> <filename> <file_size>\n"
 * 응답: "ACK <offset>\n"
 * 
 * @param s: UploadSession 구조체 포인터
 * @param id: 클라이언트 ID
 * @param file: 파일명
 * @param filesize: 예상 파일 크기
 * @return: 성공 시 0
 */
int handle_FIRST(UploadSession* s, char* id, char* file, long filesize) {
    // 세션 정보 저장
    strcpy(s->client_id, id);
    strcpy(s->filename, file);
    s->expected_size = filesize;

    // 클라이언트 ID로 디렉토리 생성 (이미 있으면 무시됨)
    mkdir(id, 0777);
    // 파일 경로 생성: ./<client_id>/<filename>
    sprintf(s->filepath, "./%s/%s", id, file);

    // 기존 파일이 있는지 확인하여 현재 저장된 크기 확인
    FILE* f = fopen(s->filepath, "rb");
    if (f) {
        // 파일이 존재하면 현재 크기를 offset으로 설정
        fseek(f, 0, SEEK_END);
        s->stored_offset = ftell(f);
        fclose(f);
    } else {
        // 파일이 없으면 offset을 0으로 설정
        s->stored_offset = 0;
    }

    // 파일을 append 모드로 열기 (이어서 쓰기 가능)
    s->fp = fopen(s->filepath, "ab");
    // 클라이언트에게 현재 offset 전송
    send_ACK(s->sd, s->stored_offset);

    return 0;
}

/**
 * handle_RESUME: RESUME 메시지 처리
 * 클라이언트가 재연결 후 업로드를 재개할 때 호출됩니다.
 * 
 * 동작:
 * 1. 기존 파일의 현재 크기를 확인
 * 2. 파일을 append 모드로 열기
 * 3. 현재 offset을 클라이언트에게 전송
 * 
 * 프로토콜: "RESUME <client_id> <filename>\n"
 * 응답: "ACK <offset>\n"
 * 
 * @param s: UploadSession 구조체 포인터
 * @param id: 클라이언트 ID
 * @param file: 파일명
 * @return: 성공 시 0
 */
int handle_RESUME(UploadSession* s, char* id, char* file) {
    // 세션 정보 저장
    strcpy(s->client_id, id);
    strcpy(s->filename, file);

    // 파일 경로 생성
    sprintf(s->filepath, "./%s/%s", id, file);

    // 기존 파일의 현재 크기 확인
    FILE* f = fopen(s->filepath, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        s->stored_offset = ftell(f);
        fclose(f);
    } else {
        s->stored_offset = 0;
    }

    // 파일을 append 모드로 열기
    s->fp = fopen(s->filepath, "ab");
    // 클라이언트에게 현재 offset 전송
    send_ACK(s->sd, s->stored_offset);

    return 0;
}

/**
 * handle_DATA: DATA 메시지 처리
 * 클라이언트가 전송한 데이터 청크를 받아서 파일에 저장합니다.
 * 
 * 동작:
 * 1. 지정된 크기만큼 데이터 수신
 * 2. 파일에 데이터 쓰기
 * 3. offset 업데이트
 * 4. 업데이트된 offset을 클라이언트에게 전송
 * 
 * 프로토콜: "DATA <chunk_size>\n" + 실제 데이터 바이너리
 * 응답: "ACK <new_offset>\n"
 * 
 * @param s: UploadSession 구조체 포인터
 * @param chunkSize: 수신할 데이터 청크 크기
 * @return: 성공 시 0, 실패 시 -1
 */
int handle_DATA(UploadSession* s, int chunkSize) {
    // 데이터를 저장할 버퍼 할당
    char* buf = malloc(chunkSize);
    if (!buf) return -1;

    // 정확히 chunkSize만큼 데이터 수신
    if (recv_exact(s->sd, buf, chunkSize) <= 0) {
        free(buf);
        return -1;
    }

    // 파일에 데이터 쓰기
    fwrite(buf, 1, chunkSize, s->fp);
    fflush(s->fp);  // 디스크에 즉시 쓰기 (데이터 손실 방지)

    // 저장된 offset 업데이트
    s->stored_offset += chunkSize;
    // 클라이언트에게 업데이트된 offset 전송
    send_ACK(s->sd, s->stored_offset);

    free(buf);
    return 0;
}

/**
 * handle_FIN: FIN 메시지 처리
 * 클라이언트가 모든 데이터 전송을 완료했다고 알릴 때 호출됩니다.
 * 
 * 동작:
 * 1. 파일 닫기
 * 2. 클라이언트에게 완료 메시지 전송
 * 
 * 프로토콜: "FIN\n"
 * 응답: "COMPLETE\n"
 * 
 * @param s: UploadSession 구조체 포인터
 * @return: 성공 시 0
 */
int handle_FIN(UploadSession* s) {
    fclose(s->fp);  // 파일 닫기
    send_COMPLETE(s->sd);  // 완료 메시지 전송
    return 0;
}
 
/* ------------------- 클라이언트 스레드 ------------------- */

/**
 * handle_client: 각 클라이언트를 처리하는 스레드 함수
 * 
 * 이 함수는 각 클라이언트 연결마다 별도의 스레드에서 실행됩니다.
 * 클라이언트로부터 메시지를 받아서 적절한 핸들러 함수를 호출합니다.
 * 
 * 처리하는 메시지 종류:
 * - FIRST: 첫 업로드 시작
 * - RESUME: 업로드 재개
 * - DATA: 데이터 청크 수신
 * - FIN: 업로드 완료
 * 
 * @param arg: 클라이언트 소켓 디스크립터를 가리키는 포인터
 * @return: NULL (스레드 종료)
 */
void* handle_client(void* arg) {
    // 클라이언트 소켓 디스크립터 가져오기
    int sd = *(int*)arg;
    
    // 업로드 세션 구조체 초기화
    UploadSession S;
    memset(&S, 0, sizeof(S));
    S.sd = sd;

    char line[512];  // 메시지를 저장할 버퍼

    // 클라이언트와 통신하는 메인 루프
    while (1) {
        // 클라이언트로부터 한 줄 메시지 수신
        if (recv_line(sd, line, sizeof(line)) <= 0)
            break;  // 연결 종료 또는 오류 발생

        // 메시지 종류에 따라 적절한 핸들러 호출
        if (strncmp(line, "FIRST", 5) == 0) {
            // FIRST 메시지: 첫 업로드 시작
            char id[64], file[256];
            long size;
            sscanf(line, "FIRST %s %s %ld", id, file, &size);
            handle_FIRST(&S, id, file, size);
        }
        else if (strncmp(line, "RESUME", 6) == 0) {
            // RESUME 메시지: 업로드 재개
            char id[64], file[256];
            sscanf(line, "RESUME %s %s", id, file);
            handle_RESUME(&S, id, file);
        }
        else if (strncmp(line, "DATA", 4) == 0) {
            // DATA 메시지: 데이터 청크 수신
            int chunk;
            sscanf(line, "DATA %d", &chunk);
            if (handle_DATA(&S, chunk) < 0)
                break;  // 데이터 수신 실패 시 종료
        }
        else if (strncmp(line, "FIN", 3) == 0) {
            // FIN 메시지: 업로드 완료
            handle_FIN(&S);
            break;  // 업로드 완료 후 종료
        }
    }

    // 클라이언트 소켓 닫기
    close(sd);
    return NULL;
}
 
/* ------------------- 서버 메인 ------------------- */

/**
 * main: 서버 프로그램의 진입점
 * 
 * 사용법: ./server <port>
 * 
 * 실행 과정:
 * 1. 명령행 인자 파싱 (포트 번호)
 * 2. 소켓 생성 및 바인딩
 * 3. 클라이언트 연결 대기 (listen)
 * 4. 클라이언트 연결 시 별도 스레드에서 처리
 * 5. 무한 루프로 계속 클라이언트 접속 대기
 */
int main(int argc, char* argv[]) {
    // 명령행 인자 개수 확인
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // TCP 소켓 생성
    int serv_sd = socket(PF_INET, SOCK_STREAM, 0);

    // 서버 주소 구조체 설정
    struct sockaddr_in serv, clnt;
    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;  // IPv4
    serv.sin_addr.s_addr = htonl(INADDR_ANY);  // 모든 네트워크 인터페이스에서 수신
    serv.sin_port = htons(atoi(argv[1]));  // 포트 번호를 네트워크 바이트 순서로 변환

    // 소켓을 주소에 바인딩
    bind(serv_sd, (struct sockaddr*)&serv, sizeof(serv));
    // 클라이언트 연결 대기 (최대 10개까지 대기 큐에 저장)
    listen(serv_sd, 10);

    printf("Server started on port %s\n", argv[1]);

    // 클라이언트 연결을 무한히 받는 메인 루프
    while (1) {
        socklen_t sz = sizeof(clnt);
        // 클라이언트 연결 수락 (블로킹 함수)
        int clnt_sd = accept(serv_sd, (struct sockaddr*)&clnt, &sz);

        // 각 클라이언트를 별도 스레드에서 처리
        pthread_t t;
        pthread_create(&t, NULL, handle_client, &clnt_sd);
        pthread_detach(t);  // 스레드가 종료되면 자동으로 정리

        printf("Connected: %s\n", inet_ntoa(clnt.sin_addr));
    }

    close(serv_sd);
    return 0;
}
 