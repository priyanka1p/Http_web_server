#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BOUNDARY "----WebKitFormBoundary7MA4YWxkTrZu0gW"
#define BUFFER_SIZE 8192

void send_file_upload(const char *filename) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // Read file contents
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("File open failed");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    char *file_contents = malloc(file_size);
    fread(file_contents, 1, file_size, fp);
    fclose(fp);

    // Build multipart/form-data body
    char header_part[1024];
    snprintf(header_part, sizeof(header_part),
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
             "Content-Type: application/octet-stream\r\n\r\n",
             BOUNDARY, filename);

    const char *footer_part = "\r\n--" BOUNDARY "--\r\n";

    int body_size = strlen(header_part) + file_size + strlen(footer_part);
    char *body = malloc(body_size + 1);
    memcpy(body, header_part, strlen(header_part));
    memcpy(body + strlen(header_part), file_contents, file_size);
    memcpy(body + strlen(header_part) + file_size, footer_part, strlen(footer_part));
    body[body_size] = '\0';

    free(file_contents);

    // Build full HTTP request
    char request_header[1024];
    snprintf(request_header, sizeof(request_header),
             "POST / HTTP/1.0\r\n"
             "Host: localhost\r\n"
             "Content-Type: multipart/form-data; boundary=%s\r\n"
             "Content-Length: %d\r\n\r\n",
             BOUNDARY, body_size);

    // Connect to server
    sock = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        free(body);
        return;
    }

    // Send header + body
    send(sock, request_header, strlen(request_header), 0);
    send(sock, body, body_size, 0);

    // Print server response
    int bytes;
    while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }

    close(sock);
    free(body);
}

int main() {
    send_file_upload("mutex_server.c");  // make sure this file exists
    return 0;
}

