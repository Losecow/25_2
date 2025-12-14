/************************************************************
 * CLIENT: Stateful File Upload with Resume
 * 
 * 이 클라이언트는 서버에 파일을 업로드하는 프로그램입니다.
 * 주요 기능:
 * - 파일을 청크 단위로 전송
 * - 연결이 끊어져도 이어서 업로드 가능 (Resume 기능)
 * - 서버와의 상태 동기화
 ************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define CHUNK 4096  // 한 번에 전송할 데이터 청크 크기 (4KB)

/* ------------------- 유틸 함수 ------------------- */

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
 * @param max: 버퍼의 최대 크기
 * @return: 수신한 바이트 수, 연결 종료 시 0, 오류 시 -1
 */
int recv_line(int sd, char* buf, int max) {
    int pos = 0;
    char c;
    while (pos < max - 1) {
        int n = recv(sd, &c, 1, 0);  // 한 바이트씩 읽기
        if (n == 0) return 0;  // 연결 종료
        if (n < 0) return -1;  // 오류 발생
        buf[pos++] = c;
        if (c == '\n') break;  // 줄바꿈 문자를 만나면 종료
    }
    buf[pos] = '\0';  // 문자열 종료 문자 추가
    return pos;
}
 
/* ------------------- 클라이언트 상태 ------------------- */

/**
 * UploadClient: 클라이언트의 업로드 상태를 저장하는 구조체
 * 
 * - sd: 서버와의 소켓 디스크립터
 * - server_ip, server_port: 서버 주소 정보
 * - client_id: 클라이언트 식별자
 * - filename: 업로드할 파일명
 * - fp: 업로드할 파일의 파일 포인터
 * - file_size: 전체 파일 크기
 * - offset: 현재까지 업로드된 위치 (서버에 저장된 바이트 수)
 */
typedef struct {
    int sd;
    char* server_ip;
    int server_port;

    char client_id[64];
    char filename[256];

    FILE* fp;
    long file_size;
    long offset;
} UploadClient;
 
/* ------------------- 서버 연결 ------------------- */

/**
 * connect_server: 서버에 TCP 연결을 시도하는 함수
 * 
 * @param uc: UploadClient 구조체 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
int connect_server(UploadClient* uc) {
    // TCP 소켓 생성
    int sd = socket(PF_INET, SOCK_STREAM, 0);

    // 서버 주소 구조체 설정
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;  // IPv4
    serv.sin_addr.s_addr = inet_addr(uc->server_ip);  // IP 주소 변환
    serv.sin_port = htons(uc->server_port);  // 포트 번호를 네트워크 바이트 순서로 변환

    // 서버에 연결 시도
    if (connect(sd, (struct sockaddr*)&serv, sizeof(serv)) < 0)
        return -1;

    // 연결 성공 시 소켓 디스크립터 저장
    uc->sd = sd;
    return 0;
}
 
/* ------------------- 메시지 핸들러 ------------------- */

/**
 * send_FIRST: 첫 업로드 시작 메시지를 서버에 전송
 * 서버는 이미 저장된 파일이 있으면 그 위치를 알려주고,
 * 없으면 0을 반환합니다.
 * 
 * 프로토콜: "FIRST <client_id> <filename> <file_size>\n"
 * 응답: "ACK <offset>\n"
 * 
 * @param uc: UploadClient 구조체 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
int send_FIRST(UploadClient* uc) {
    char msg[256];
    sprintf(msg, "FIRST %s %s %ld\n",
            uc->client_id, uc->filename, uc->file_size);

    if (send_line(uc->sd, msg) < 0)
        return -1;

    // 서버로부터 ACK 응답 수신
    char line[128];
    if (recv_line(uc->sd, line, sizeof(line)) <= 0)
        return -1;

    // 서버가 알려준 offset(이미 저장된 바이트 수)를 파싱
    sscanf(line, "ACK %ld", &uc->offset);
    // 파일 포인터를 offset 위치로 이동 (이미 업로드된 부분은 건너뛰기)
    fseek(uc->fp, uc->offset, SEEK_SET);

    return 0;
}

/**
 * send_RESUME: 재연결 후 업로드 재개 메시지를 서버에 전송
 * 연결이 끊어진 후 다시 연결했을 때 사용합니다.
 * 
 * 프로토콜: "RESUME <client_id> <filename>\n"
 * 응답: "ACK <offset>\n"
 * 
 * @param uc: UploadClient 구조체 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
int send_RESUME(UploadClient* uc) {
    char msg[256];
    sprintf(msg, "RESUME %s %s\n", uc->client_id, uc->filename);

    if (send_line(uc->sd, msg) < 0)
        return -1;

    // 서버로부터 현재 저장된 offset 수신
    char line[128];
    if (recv_line(uc->sd, line, sizeof(line)) <= 0)
        return -1;

    sscanf(line, "ACK %ld", &uc->offset);
    // 파일 포인터를 offset 위치로 이동
    fseek(uc->fp, uc->offset, SEEK_SET);

    return 0;
}

/**
 * send_DATA_chunk: 데이터 청크를 서버에 전송
 * 먼저 헤더("DATA <size>\n")를 보내고, 그 다음 실제 데이터를 전송합니다.
 * 서버는 수신 후 업데이트된 offset을 ACK로 보냅니다.
 * 
 * 프로토콜: "DATA <chunk_size>\n" + 실제 데이터 바이너리
 * 응답: "ACK <new_offset>\n"
 * 
 * @param uc: UploadClient 구조체 포인터
 * @param buf: 전송할 데이터 버퍼
 * @param size: 전송할 데이터 크기
 * @return: 성공 시 0, 실패 시 -1
 */
int send_DATA_chunk(UploadClient* uc, char* buf, int size) {
    // 데이터 크기를 알리는 헤더 전송
    char header[64];
    sprintf(header, "DATA %d\n", size);

    if (send_line(uc->sd, header) < 0)
        return -1;

    // 실제 데이터 전송
    if (safe_send(uc->sd, buf, size) < 0)
        return -1;

    // 서버로부터 업데이트된 offset 수신
    char line[128];
    if (recv_line(uc->sd, line, sizeof(line)) <= 0)
        return -1;

    sscanf(line, "ACK %ld", &uc->offset);

    return 0;
}

/**
 * send_FIN: 업로드 완료 메시지를 서버에 전송
 * 모든 데이터 전송이 끝났을 때 호출합니다.
 * 
 * 프로토콜: "FIN\n"
 * 응답: "COMPLETE\n"
 * 
 * @param uc: UploadClient 구조체 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
int send_FIN(UploadClient* uc) {
    if (send_line(uc->sd, "FIN\n") < 0)
        return -1;

    char line[128];
    if (recv_line(uc->sd, line, sizeof(line)) <= 0)
        return -1;

    // 서버가 "COMPLETE"를 보내면 업로드 성공
    if (strncmp(line, "COMPLETE", 8) == 0)
        return 0;

    return -1;
}
 
/* ------------------- 업로드 로직 ------------------- */

/**
 * upload_file: 파일을 청크 단위로 업로드하는 메인 로직
 * 
 * 동작 과정:
 * 1. offset 위치부터 파일을 읽어서 청크 단위로 전송
 * 2. 전송 실패 시 연결을 끊고 재연결
 * 3. 재연결 후 RESUME 메시지로 이어서 업로드
 * 4. 모든 데이터 전송 완료 후 FIN 메시지 전송
 * 
 * @param uc: UploadClient 구조체 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
int upload_file(UploadClient* uc) {
    char buf[CHUNK];

    // offset이 file_size에 도달할 때까지 반복
    while (uc->offset < uc->file_size) {
        // 파일 포인터를 현재 offset 위치로 이동
        fseek(uc->fp, uc->offset, SEEK_SET);
        // CHUNK 크기만큼 파일에서 읽기
        int n = fread(buf, 1, CHUNK, uc->fp);
        if (n <= 0) break;  // 읽을 데이터가 없으면 종료

        // 데이터 청크 전송 시도
        if (send_DATA_chunk(uc, buf, n) == 0)
            continue;  // 성공하면 다음 청크로 계속

        // 전송 실패 시 재연결 시도
        printf("⚠️ send 실패 → 재접속 시도 중...\n");
        close(uc->sd);  // 기존 소켓 닫기

        // 서버에 재연결 시도 (성공할 때까지 반복)
        while (connect_server(uc) < 0)
            sleep(1);  // 1초 대기 후 재시도

        // RESUME 메시지로 업로드 재개
        if (send_RESUME(uc) < 0)
            return -1;
    }

    // 모든 데이터 전송 완료 후 FIN 메시지 전송
    return send_FIN(uc);
}
 
/* ------------------- 클라이언트 메인 ------------------- */

/**
 * main: 클라이언트 프로그램의 진입점
 * 
 * 사용법: ./client <IP> <port> <ClientID> <File>
 * 
 * 실행 과정:
 * 1. 명령행 인자 파싱
 * 2. 업로드할 파일 열기 및 크기 확인
 * 3. 서버에 연결
 * 4. FIRST 메시지 전송하여 업로드 시작
 * 5. 파일 업로드 실행
 * 6. 정리 작업 (파일 닫기, 소켓 닫기)
 */
int main(int argc, char* argv[]) {
    // 명령행 인자 개수 확인
    if (argc != 5) {
        printf("Usage: %s <IP> <port> <ClientID> <File>\n", argv[0]);
        exit(1);
    }

    // UploadClient 구조체 초기화
    UploadClient uc;
    uc.server_ip = argv[1];        // 서버 IP 주소
    uc.server_port = atoi(argv[2]); // 서버 포트 번호
    strcpy(uc.client_id, argv[3]);  // 클라이언트 ID
    strcpy(uc.filename, argv[4]);   // 업로드할 파일명

    // 업로드할 파일 열기 (바이너리 읽기 모드)
    uc.fp = fopen(uc.filename, "rb");
    if (!uc.fp) {
        printf("파일을 열 수 없음: %s\n", uc.filename);
        exit(1);
    }

    // 파일 크기 계산
    fseek(uc.fp, 0, SEEK_END);      // 파일 끝으로 이동
    uc.file_size = ftell(uc.fp);     // 현재 위치(파일 크기) 저장
    fseek(uc.fp, 0, SEEK_SET);       // 파일 시작 위치로 복귀

    // 서버에 연결 시도 (성공할 때까지 반복)
    while (connect_server(&uc) < 0)
        sleep(1);  // 연결 실패 시 1초 대기 후 재시도

    // FIRST 메시지 전송하여 업로드 시작
    // 서버는 이미 저장된 파일이 있으면 그 위치를 알려줌
    if (send_FIRST(&uc) < 0) {
        printf("FIRST 실패\n");
        return -1;
    }

    // 실제 파일 업로드 실행
    upload_file(&uc);

    // 정리 작업
    fclose(uc.fp);   // 파일 닫기
    close(uc.sd);    // 소켓 닫기
    return 0;
}
 