#include "common.h"

#define LINE_BUF 2048

static int send_handshake(int sock, const char *client_id, const char *filename, off_t filesize, off_t current_offset) {
    char line[LINE_BUF];
    if (current_offset == 0) {
        snprintf(line, sizeof(line), "FIRST %s %s %lld\n", client_id, filename, (long long)filesize);
    } else {
        snprintf(line, sizeof(line), "RESUME %s %s\n", client_id, filename);
    }
    if (send_all(sock, line, strlen(line)) < 0) {
        return -1;
    }
    ssize_t n = recv_line(sock, line, sizeof(line));
    if (n <= 0) {
        return -1;
    }
    long long ack_offset = 0;
    if (sscanf(line, "ACK %lld", &ack_offset) != 1) {
        fprintf(stderr, "ACK 파싱 실패: %s\n", line);
        return -1;
    }
    if (ack_offset < 0 || ack_offset > filesize) {
        fprintf(stderr, "ACK 오프셋 범위 오류: %lld\n", ack_offset);
        return -1;
    }
    return (int)ack_offset;
}

static int upload_file(const char *ip, uint16_t port, const char *client_id, const char *filepath) {
    struct stat st;
    if (stat(filepath, &st) != 0) {
        perror("stat");
        return -1;
    }
    off_t filesize = st.st_size;
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    off_t offset = 0;
    while (offset < filesize) {
        int sock = connect_to_server(ip, port);
        if (sock < 0) {
            fprintf(stderr, "서버 연결 실패, 재시도 필요\n");
            sleep(1);
            continue;
        }

        int ack = send_handshake(sock, client_id, filename, filesize, offset);
        if (ack < 0) {
            fprintf(stderr, "핸드셰이크 실패\n");
            close(sock);
            sleep(1);
            continue;
        }
        offset = (off_t)ack;

        FILE *fp = fopen(filepath, "rb");
        if (!fp) {
            perror("fopen");
            close(sock);
            return -1;
        }
        if (fseeko(fp, offset, SEEK_SET) != 0) {
            perror("fseeko");
            fclose(fp);
            close(sock);
            return -1;
        }

        char line[LINE_BUF];
        char buf[BUFFER_SIZE];
        int error = 0;
        while (offset < filesize) {
            size_t to_read = (size_t)((filesize - offset) < BUFFER_SIZE ? (filesize - offset) : BUFFER_SIZE);
            size_t r = fread(buf, 1, to_read, fp);
            if (r != to_read) {
                fprintf(stderr, "파일 읽기 실패\n");
                error = 1;
                break;
            }
            snprintf(line, sizeof(line), "DATA %zu\n", r);
            if (send_all(sock, line, strlen(line)) < 0) {
                error = 1;
                break;
            }
            if (send_all(sock, buf, r) < 0) {
                error = 1;
                break;
            }
            offset += (off_t)r;
        }

        if (!error) {
            const char *fin = "FIN\n";
            if (send_all(sock, fin, strlen(fin)) == 0) {
                ssize_t n = recv_line(sock, line, sizeof(line));
                if (n > 0 && strncmp(line, "COMPLETE", 8) == 0) {
                    fclose(fp);
                    close(sock);
                    return 0;
                }
            }
            // fallthrough to retry
        }

        fprintf(stderr, "전송 중단, 재시도 offset=%lld\n", (long long)offset);
        fclose(fp);
        close(sock);
        sleep(1);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "사용법: %s <server_ip> <client_id> <file_path> [port]\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    const char *client_id = argv[2];
    const char *file_path = argv[3];
    uint16_t port = (argc >= 5) ? (uint16_t)atoi(argv[4]) : DEFAULT_PORT;

    if (upload_file(server_ip, port, client_id, file_path) == 0) {
        printf("업로드 완료\n");
        return 0;
    }

    fprintf(stderr, "업로드 실패\n");
    return 1;
}

