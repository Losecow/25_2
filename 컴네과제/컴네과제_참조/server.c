/************************************************************
 * SERVER: Stateful File Upload with Resume Capability
 ************************************************************/
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <arpa/inet.h>
 #include <pthread.h>
 #include <sys/stat.h>
 
 #define BUF_SIZE 4096
 
 /* ------------------- 공통 유틸 함수 ------------------- */
 
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
 
 int recv_line(int sd, char* buf, int maxlen) {
     int pos = 0;
     char c;
 
     while (pos < maxlen - 1) {
         int n = recv(sd, &c, 1, 0);
         if (n == 0) return 0;
         if (n < 0) return -1;
 
         buf[pos++] = c;
         if (c == '\n') break;
     }
     buf[pos] = '\0';
     return pos;
 }
 
 int recv_exact(int sd, char* buf, int size) {
     int received = 0;
     while (received < size) {
         int n = recv(sd, buf + received, size - received, 0);
         if (n <= 0) return -1;
         received += n;
     }
     return received;
 }
 
 /* ------------------- 업로드 세션 ------------------- */
 
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
 
 void send_ACK(int sd, long offset) {
     char msg[64];
     sprintf(msg, "ACK %ld\n", offset);
     send_line(sd, msg);
 }
 
 void send_COMPLETE(int sd) {
     send_line(sd, "COMPLETE\n");
 }
 
 /* FIRST */
 int handle_FIRST(UploadSession* s, char* id, char* file, long filesize) {
     strcpy(s->client_id, id);
     strcpy(s->filename, file);
     s->expected_size = filesize;
 
     mkdir(id, 0777);
     sprintf(s->filepath, "./%s/%s", id, file);
 
     FILE* f = fopen(s->filepath, "rb");
     if (f) {
         fseek(f, 0, SEEK_END);
         s->stored_offset = ftell(f);
         fclose(f);
     } else s->stored_offset = 0;
 
     s->fp = fopen(s->filepath, "ab");
     send_ACK(s->sd, s->stored_offset);
 
     return 0;
 }
 
 /* RESUME */
 int handle_RESUME(UploadSession* s, char* id, char* file) {
     strcpy(s->client_id, id);
     strcpy(s->filename, file);
 
     sprintf(s->filepath, "./%s/%s", id, file);
 
     FILE* f = fopen(s->filepath, "rb");
     if (f) {
         fseek(f, 0, SEEK_END);
         s->stored_offset = ftell(f);
         fclose(f);
     } else s->stored_offset = 0;
 
     s->fp = fopen(s->filepath, "ab");
     send_ACK(s->sd, s->stored_offset);
 
     return 0;
 }
 
 /* DATA */
 int handle_DATA(UploadSession* s, int chunkSize) {
     char* buf = malloc(chunkSize);
     if (!buf) return -1;
 
     if (recv_exact(s->sd, buf, chunkSize) <= 0) {
         free(buf);
         return -1;
     }
 
     fwrite(buf, 1, chunkSize, s->fp);
     fflush(s->fp);
 
     s->stored_offset += chunkSize;
     send_ACK(s->sd, s->stored_offset);
 
     free(buf);
     return 0;
 }
 
 /* FIN */
 int handle_FIN(UploadSession* s) {
     fclose(s->fp);
     send_COMPLETE(s->sd);
     return 0;
 }
 
 /* ------------------- 클라이언트 스레드 ------------------- */
 
 void* handle_client(void* arg) {
     int sd = *(int*)arg;
     UploadSession S;
     memset(&S, 0, sizeof(S));
     S.sd = sd;
 
     char line[512];
 
     while (1) {
         if (recv_line(sd, line, sizeof(line)) <= 0)
             break;
 
         if (strncmp(line, "FIRST", 5) == 0) {
             char id[64], file[256];
             long size;
             sscanf(line, "FIRST %s %s %ld", id, file, &size);
             handle_FIRST(&S, id, file, size);
         }
         else if (strncmp(line, "RESUME", 6) == 0) {
             char id[64], file[256];
             sscanf(line, "RESUME %s %s", id, file);
             handle_RESUME(&S, id, file);
         }
         else if (strncmp(line, "DATA", 4) == 0) {
             int chunk;
             sscanf(line, "DATA %d", &chunk);
             if (handle_DATA(&S, chunk) < 0)
                 break;
         }
         else if (strncmp(line, "FIN", 3) == 0) {
             handle_FIN(&S);
             break;
         }
     }
 
     close(sd);
     return NULL;
 }
 
 /* ------------------- 서버 메인 ------------------- */
 
 int main(int argc, char* argv[]) {
     if (argc != 2) {
         printf("Usage: %s <port>\n", argv[0]);
         exit(1);
     }
 
     int serv_sd = socket(PF_INET, SOCK_STREAM, 0);
 
     struct sockaddr_in serv, clnt;
     memset(&serv, 0, sizeof(serv));
 
     serv.sin_family = AF_INET;
     serv.sin_addr.s_addr = htonl(INADDR_ANY);
     serv.sin_port = htons(atoi(argv[1]));
 
     bind(serv_sd, (struct sockaddr*)&serv, sizeof(serv));
     listen(serv_sd, 10);
 
     printf("Server started on port %s\n", argv[1]);
 
     while (1) {
         socklen_t sz = sizeof(clnt);
         int clnt_sd = accept(serv_sd, (struct sockaddr*)&clnt, &sz);
 
         pthread_t t;
         pthread_create(&t, NULL, handle_client, &clnt_sd);
         pthread_detach(t);
 
         printf("Connected: %s\n", inet_ntoa(clnt.sin_addr));
     }
 
     close(serv_sd);
     return 0;
 }
 