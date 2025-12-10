/************************************************************
 * CLIENT: Stateful File Upload with Resume
 ************************************************************/
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <arpa/inet.h>
 
 #define CHUNK 4096
 
 /* ------------------- 유틸 함수 ------------------- */
 
 int safe_send(int sd, const char* data, int size) {
     int sent = 0;
     while (sent < size) {
         int n = send(sd, data + sent, size - sent, 0);
         if (n <= 0) return -1;
         sent += n;
     }
     return sent;
 }
 
 int send_line(int sd, const char* msg) {
     return safe_send(sd, msg, strlen(msg));
 }
 
 int recv_line(int sd, char* buf, int max) {
     int pos = 0;
     char c;
     while (pos < max - 1) {
         int n = recv(sd, &c, 1, 0);
         if (n == 0) return 0;
         if (n < 0) return -1;
         buf[pos++] = c;
         if (c == '\n') break;
     }
     buf[pos] = '\0';
     return pos;
 }
 
 /* ------------------- 클라이언트 상태 ------------------- */
 
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
 
 int connect_server(UploadClient* uc) {
     int sd = socket(PF_INET, SOCK_STREAM, 0);
 
     struct sockaddr_in serv;
     memset(&serv, 0, sizeof(serv));
     serv.sin_family = AF_INET;
     serv.sin_addr.s_addr = inet_addr(uc->server_ip);
     serv.sin_port = htons(uc->server_port);
 
     if (connect(sd, (struct sockaddr*)&serv, sizeof(serv)) < 0)
         return -1;
 
     uc->sd = sd;
     return 0;
 }
 
 /* ------------------- 메시지 핸들러 ------------------- */
 
 int send_FIRST(UploadClient* uc) {
     char msg[256];
     sprintf(msg, "FIRST %s %s %ld\n",
             uc->client_id, uc->filename, uc->file_size);
 
     if (send_line(uc->sd, msg) < 0)
         return -1;
 
     char line[128];
     if (recv_line(uc->sd, line, sizeof(line)) <= 0)
         return -1;
 
     sscanf(line, "ACK %ld", &uc->offset);
     fseek(uc->fp, uc->offset, SEEK_SET);
 
     return 0;
 }
 
 int send_RESUME(UploadClient* uc) {
     char msg[256];
     sprintf(msg, "RESUME %s %s\n", uc->client_id, uc->filename);
 
     if (send_line(uc->sd, msg) < 0)
         return -1;
 
     char line[128];
     if (recv_line(uc->sd, line, sizeof(line)) <= 0)
         return -1;
 
     sscanf(line, "ACK %ld", &uc->offset);
     fseek(uc->fp, uc->offset, SEEK_SET);
 
     return 0;
 }
 
 int send_DATA_chunk(UploadClient* uc, char* buf, int size) {
     char header[64];
     sprintf(header, "DATA %d\n", size);
 
     if (send_line(uc->sd, header) < 0)
         return -1;
 
     if (safe_send(uc->sd, buf, size) < 0)
         return -1;
 
     char line[128];
     if (recv_line(uc->sd, line, sizeof(line)) <= 0)
         return -1;
 
     sscanf(line, "ACK %ld", &uc->offset);
 
     return 0;
 }
 
 int send_FIN(UploadClient* uc) {
     if (send_line(uc->sd, "FIN\n") < 0)
         return -1;
 
     char line[128];
     if (recv_line(uc->sd, line, sizeof(line)) <= 0)
         return -1;
 
     if (strncmp(line, "COMPLETE", 8) == 0)
         return 0;
 
     return -1;
 }
 
 /* ------------------- 업로드 로직 ------------------- */
 
 int upload_file(UploadClient* uc) {
     char buf[CHUNK];
 
     while (uc->offset < uc->file_size) {
         fseek(uc->fp, uc->offset, SEEK_SET);
         int n = fread(buf, 1, CHUNK, uc->fp);
         if (n <= 0) break;
 
         if (send_DATA_chunk(uc, buf, n) == 0)
             continue;
 
         printf("⚠️ send 실패 → 재접속 시도 중...\n");
         close(uc->sd);
 
         while (connect_server(uc) < 0)
             sleep(1);
 
         if (send_RESUME(uc) < 0)
             return -1;
     }
 
     return send_FIN(uc);
 }
 
 /* ------------------- 클라이언트 메인 ------------------- */
 
 int main(int argc, char* argv[]) {
     if (argc != 5) {
         printf("Usage: %s <IP> <port> <ClientID> <File>\n", argv[0]);
         exit(1);
     }
 
     UploadClient uc;
     uc.server_ip = argv[1];
     uc.server_port = atoi(argv[2]);
     strcpy(uc.client_id, argv[3]);
     strcpy(uc.filename, argv[4]);
 
     uc.fp = fopen(uc.filename, "rb");
     if (!uc.fp) {
         printf("파일을 열 수 없음: %s\n", uc.filename);
         exit(1);
     }
 
     fseek(uc.fp, 0, SEEK_END);
     uc.file_size = ftell(uc.fp);
     fseek(uc.fp, 0, SEEK_SET);
 
     // 서버 연결
     while (connect_server(&uc) < 0)
         sleep(1);
 
     // FIRST 전송
     if (send_FIRST(&uc) < 0) {
         printf("FIRST 실패\n");
         return -1;
     }
 
     // 실제 업로드
     upload_file(&uc);
 
     fclose(uc.fp);
     close(uc.sd);
     return 0;
 }
 