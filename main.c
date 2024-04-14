#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>

#include "utils.h"

#define port 8080
#define buff_size 10240
#define max_request 100
#define response_size 1024
#define file_name_size 20

regex_t regex;
size_t memo_index = 0;
struct timeval t0, t1;
struct resp_memo {
    size_t length;
    char response[response_size];
    char file_name[file_name_size];
} memo[5] = {};

void *handle_client(void *agr);
void build_http_response(const char *file_name, char *response);

void exit_in_failure(int status, const char *message){
    if (status == -1) { perror(message); exit(EXIT_FAILURE); }
}

int main(int argc, char *argv[]) {
    // initialization
    short reg_status = regcomp(&regex, "^GET /([^ ]*) HTTP/1", REG_EXTENDED);
    exit_in_failure(reg_status, "regex compilation failed");
    // initialization-end

    struct sockaddr_in server_addr;

    short server_fd = socket(AF_INET, SOCK_STREAM, 0);
    exit_in_failure(server_fd, "socket failed");

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    short bind_status = bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    exit_in_failure(bind_status, "bind failed");

    short listen_status = listen(server_fd, max_request);
    exit_in_failure(listen_status, "listen failed");

    while (1) {
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	int *client_fd = malloc(sizeof(int));
	*client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
	if (*client_fd == 1) { perror("accept failed"); continue; }

	pthread_t thread_id;
	pthread_create(&thread_id, NULL, handle_client, (void *)client_fd);
	pthread_detach(thread_id);
    }

    close(server_fd);
    regfree(&regex);
    return 0;
}

void *handle_client(void *arg) {
    gettimeofday(&t0, NULL);

    int client_fd = *((int *)arg);
    char *buffer = (char *)malloc(buff_size * sizeof(char));

    regmatch_t matches[2];
    ssize_t bytes_received = recv(client_fd, buffer, buff_size, 0);
    short regexec_status = regexec(&regex, buffer, 2, matches, 0);

    if(bytes_received > 0 && regexec_status == 0) {
	buffer[matches[1].rm_eo] = '\0';

	const char *url_enconded_file_name = buffer + matches[1].rm_so;
	char *file_name = url_decode(url_enconded_file_name);

	size_t index = -1;
	bool is_cached = false;
	for (size_t i = 0; i < memo_index; i++) {
	    if (strcmp(file_name, memo[i].file_name) == 0) {
		index = i;
		is_cached = true;
		break;
	    }
	}

	char *response = (char *)malloc(buff_size * sizeof(char));
	build_http_response(file_name, response);
	send(client_fd, response, strlen(response), 0);
	if (is_cached) {
	    send(client_fd, memo[index].response, memo[index].length, 0);
	    printf("cached %s\n", memo[index].file_name);
	} else {
	    char *response = (char *)malloc(buff_size * sizeof(char));
	    build_http_response(file_name, response);
	    send(client_fd, response, strlen(response), 0);

	    free(response);
	}

	free(response);

	free(file_name);
    }

    close(client_fd);
    free(arg);
    free(buffer);

    gettimeofday(&t1, NULL);
    printf("Did it in %.2g seconds\n", t1.tv_sec - t0.tv_sec + 1E-6 * (t1.tv_usec - t0.tv_usec));

    return NULL;
}

void build_http_response(const char *file_name, char *response) {
    int file_fd = open(file_name, O_RDONLY);
    FILE *fp = fopen(file_name, "r");

    if (fp == NULL || file_fd == -1) {
	printf("not found\n");
	snprintf(response, buff_size, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\n404 Not Found");
	return;
    }

    printf("found %s\n", file_name);

    char file_ext[32];
    strcpy(file_ext, get_file_extension(file_name));
    const char *mime_type = get_mime_type(file_ext);
    snprintf(response, buff_size, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", mime_type);

    struct stat file_stat;
    fstat(file_fd, &file_stat);
    off_t file_size = file_stat.st_size;

    fread(response+strlen(response), sizeof(char), file_size, fp);

    strncpy(memo[memo_index].response, response, response_size);
    strncpy(memo[memo_index].file_name, file_name, 10);
    memo[memo_index].length = strlen(response);
    if (memo_index < 5) { memo_index++; }

    close(file_fd);
    fclose(fp);
}
