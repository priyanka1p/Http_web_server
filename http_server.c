#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/epoll.h>

#define PORT 8080
#define BUFFER_SIZE 8192
#define MAX_EVENTS 100
#define WEB_ROOT "/var/www"
#define UPLOAD_DIR "/var/www/uploads"

const char* get_content_type(const char* path) {
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".jpg")) return "image/jpeg";
    if (strstr(path, ".png")) return "image/png";
    if (strstr(path, ".gif")) return "image/gif";
    if (strstr(path, ".css")) return "text/css";
    if (strstr(path, ".js")) return "application/javascript";
    if (strstr(path, ".pdf")) return "application/pdf";
    if (strstr(path, ".mp4")) return "video/mp4";
    if (strstr(path, ".mp3")) return "audio/mpeg";
    return "application/octet-stream";
}

void save_uploaded_file(const char* boundary, const char* body, const char* end) {
    const char* file_start = strstr(body, "\r\n\r\n");
    if (!file_start) {
        printf("File start not found.\n");
        return;
    }
    file_start += 4;

    const char* file_end = strstr(file_start, boundary);
    if (!file_end) {
        printf("File end not found.\n");
        return;
    }

    const char* filename_marker = strstr(body, "filename=\"");
    if (!filename_marker) {
        printf("Filename not found.\n");
        return;
    }
    filename_marker += 10;
    const char* filename_end = strchr(filename_marker, '"');
    if (!filename_end) return;

    char filename[256];
    snprintf(filename, filename_end - filename_marker + 1, "%s", filename_marker);

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", UPLOAD_DIR, filename);

    FILE* fp = fopen(filepath, "wb");
    if (fp) {
        fwrite(file_start, 1, file_end - file_start - 2, fp); // remove \r\n
        fclose(fp);
        printf("Saved file: %s\n", filepath);
    } else {
        perror("File write failed");
    }
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void handle_request(int client_fd) {
    char buffer[BUFFER_SIZE];
    int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        close(client_fd);
        return;
    }
    buffer[bytes] = '\0';

    char method[16], path_raw[2048], protocol[16];
    sscanf(buffer, "%15s %2047s %15s", method, path_raw, protocol);

    if (strcmp(method, "GET") == 0) {
        char path[4096];
        if (strcmp(path_raw, "/") == 0) {
            snprintf(path, sizeof(path), "%s/index.html", WEB_ROOT);
        } else {
            snprintf(path, sizeof(path), "%s%s", WEB_ROOT, path_raw);
        }

        FILE* file = fopen(path, "rb");
        if (!file) {
            const char* not_found = "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile not found.\n";
            send(client_fd, not_found, strlen(not_found), 0);
        } else {
            fseek(file, 0, SEEK_END);
            long filesize = ftell(file);
            rewind(file);

            const char* content_type = get_content_type(path);
            char header[512];
            snprintf(header, sizeof(header),
                     "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n",
                     content_type, filesize);
            send(client_fd, header, strlen(header), 0);

            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                send(client_fd, buffer, n, 0);
            }

            fclose(file);
        }

    } else if (strcmp(method, "POST") == 0) {
        const char* content_length_header = strstr(buffer, "Content-Length: ");
        if (!content_length_header) {
            printf("Missing Content-Length\n");
            close(client_fd);
            return;
        }

        int content_length = 0;
        sscanf(content_length_header, "Content-Length: %d", &content_length);

        const char* header_end = strstr(buffer, "\r\n\r\n");
        if (!header_end) {
            printf("No header end found\n");
            close(client_fd);
            return;
        }
        header_end += 4;

        int header_bytes = header_end - buffer;
        int body_bytes = bytes - header_bytes;

        char* full_body = malloc(content_length + 1);
        if (!full_body) {
            perror("malloc failed");
            close(client_fd);
            return;
        }

        memcpy(full_body, header_end, body_bytes);

        int total_read = body_bytes;
        while (total_read < content_length) {
            int n = recv(client_fd, full_body + total_read, content_length - total_read, 0);
            if (n <= 0) break;
            total_read += n;
        }
        full_body[content_length] = '\0';

        if (strstr(buffer, "multipart/form-data")) {
            const char* boundary_marker = strstr(buffer, "boundary=");
            if (!boundary_marker) {
                printf("Boundary not found.\n");
                free(full_body);
                close(client_fd);
                return;
            }

            char boundary[100];
            sscanf(boundary_marker, "boundary=%99s", boundary);
            char real_boundary[120] = "--";
            strcat(real_boundary, boundary);
            printf("Boundary: %s\n", real_boundary);

            save_uploaded_file(real_boundary, full_body, full_body + content_length);

            const char* ok_response = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nFile uploaded successfully.\n";
            send(client_fd, ok_response, strlen(ok_response), 0);

        } else if (strstr(buffer, "application/x-www-form-urlencoded")) {
            FILE* f = fopen(UPLOAD_DIR "/form_data.txt", "a");
            if (f) {
                fprintf(f, "%s\n", full_body);
                fclose(f);
            }

            char response[1024];
            snprintf(response, sizeof(response),
                     "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nReceived POST data:\n%s\n", full_body);
            send(client_fd, response, strlen(response), 0);
        }

        free(full_body);
    }

    close(client_fd);
}

int main() {
    mkdir(UPLOAD_DIR, 0755);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(server_fd);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    int epoll_fd = epoll_create1(0);
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    printf("HTTP Server with epoll running at http://localhost:%d\n", PORT);

    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == server_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd >= 0) {
                    set_nonblocking(client_fd);
                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                }
            } else {
                handle_request(events[i].data.fd);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
            }
        }
    }

    close(server_fd);
    return 0;
}
