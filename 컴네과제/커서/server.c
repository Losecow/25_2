#include "common.h"

#define LINE_BUF 2048

static int ensure_dir(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (errno != ENOENT) {
        return -1;
    }
    return mkdir(dir, 0755);
}

static off_t file_size_if_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

static int handle_transfer(int client_fd, const char *client_id, const char *filename, off_t offset) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", client_id, filename);

    FILE *fp = fopen(filepath, "ab+");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    if (fseeko(fp, offset, SEEK_SET) != 0) {
        perror("fseeko");
        fclose(fp);
        return -1;
    }

    char line[LINE_BUF];
    while (1) {
        ssize_t n = recv_line(client_fd, line, sizeof(line));
        if (n <= 0) {
            fprintf(stderr, "연결 종료 또는 읽기 실패\n");
            fclose(fp);
            return -1;
        }

        if (strncmp(line, "DATA", 4) == 0) {
            long chunk = 0;
            if (sscanf(line, "DATA %ld", &chunk) != 1 || chunk <= 0) {
                fprintf(stderr, "DATA 형식 오류: %s\n", line);
                fclose(fp);
                return -1;
            }
            char *buf = (char *)malloc((size_t)chunk);
            if (!buf) {
                fprintf(stderr, "메모리 부족\n");
                fclose(fp);
                return -1;
            }
            if (recv_all(client_fd, buf, (size_t)chunk) < 0) {
                fprintf(stderr, "데이터 수신 실패\n");
                free(buf);
                fclose(fp);
                return -1;
            }
            size_t written = fwrite(buf, 1, (size_t)chunk, fp);
            free(buf);
            if (written != (size_t)chunk) {
                fprintf(stderr, "파일 기록 실패\n");
                fclose(fp);
                return -1;
            }
            fflush(fp);
            continue;
        }

        if (strncmp(line, "FIN", 3) == 0) {
            const char *ok = "COMPLETE\n";
            send_all(client_fd, ok, strlen(ok));
            fclose(fp);
            return 0;
        }

        fprintf(stderr, "알 수 없는 메시지: %s\n", line);
        fclose(fp);
        return -1;
    }
}

static int handle_client(int client_fd) {
    char line[LINE_BUF];
    ssize_t n = recv_line(client_fd, line, sizeof(line));
    if (n <= 0) {
        return -1;
    }

    char cmd[16], client_id[128], filename[256];
    long long file_size = 0;
    off_t offset = 0;
    int is_resume = 0;

    if (sscanf(line, "%15s %127s %255s %lld", cmd, client_id, filename, &file_size) >= 3) {
        if (strcmp(cmd, "FIRST") == 0) {
            // ensure directory and check existing size
            if (ensure_dir(client_id) < 0) {
                fprintf(stderr, "디렉토리 생성 실패\n");
                return -1;
            }
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s", client_id, filename);
            offset = file_size_if_exists(filepath);
        } else if (strcmp(cmd, "RESUME") == 0) {
            is_resume = 1;
            if (ensure_dir(client_id) < 0) {
                fprintf(stderr, "디렉토리 준비 실패\n");
                return -1;
            }
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s", client_id, filename);
            offset = file_size_if_exists(filepath);
        } else {
            fprintf(stderr, "알 수 없는 명령: %s\n", cmd);
            return -1;
        }
    } else {
        fprintf(stderr, "헤더 파싱 실패: %s\n", line);
        return -1;
    }

    char ack[64];
    snprintf(ack, sizeof(ack), "ACK %lld\n", (long long)offset);
    if (send_all(client_fd, ack, strlen(ack)) < 0) {
        fprintf(stderr, "ACK 전송 실패\n");
        return -1;
    }

    (void)is_resume; // 현재 로직에서는 재개 여부만 구분, 별도 처리 없음
    return handle_transfer(client_fd, client_id, filename, offset);
}

int main(int argc, char *argv[]) {
    uint16_t port = (argc >= 2) ? (uint16_t)atoi(argv[1]) : DEFAULT_PORT;

    int server_fd = create_server_socket(port, BACKLOG);
    if (server_fd < 0) {
        return 1;
    }
    printf("서버 시작 (포트 %u)\n", port);

    while (1) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int cfd = accept(server_fd, (struct sockaddr *)&cliaddr, &clilen);
        if (cfd < 0) {
            perror("accept");
            continue;
        }

        printf("클라이언트 접속: %s\n", inet_ntoa(cliaddr.sin_addr));
        if (handle_client(cfd) < 0) {
            fprintf(stderr, "클라이언트 처리 중 오류\n");
        }
        close(cfd);
    }

    close(server_fd);
    return 0;
}

