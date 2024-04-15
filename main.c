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

#define is_debug_enable true
#define port 8080
#define buff_size 2056
#define max_request 100
#define file_name_size 20
#define file_path_size 30
#define static_folder "static/"

#if is_debug_enable
    struct timeval t0, t1;
#endif

regex_t regex;
size_t memo_index = 0;
struct resp_memo {
    size_t length;
    char response[buff_size];
    char file_name[file_name_size];
} memo[5] = {};

void *handle_client(void *agr);
void build_http_response(const char *file_name, char *response);

const char *get_file_extension(const char *file_name) {
    const char *dot = strrchr(file_name, '.');
    if (dot == NULL || dot == file_name) { return ""; }
    return dot + 1;
}

const char *get_mime_type(const char *file_ext) {
    switch (file_ext[0]) {
	case 'h': return "text/html";
	case 't': return "text/plain";
	case 'j': return "image/jpeg";
	case 'p': return "image/png";
	default: return "text/html";
    }
}

void exit_in_failure(int status, const char *message){
    if (status == -1) { perror(message); exit(EXIT_FAILURE); }
}

void load_memo() {
    const char default_response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\n404 Not Found";
    snprintf(memo[memo_index].response, buff_size, default_response);
    snprintf(memo[memo_index].file_name, buff_size, "default");
    memo[memo_index].length = strlen(default_response);
    memo_index++;

    struct dirent *ent;
    DIR *dir = opendir(static_folder);

    if (dir == NULL) {
	perror("Could not open folder"); 
	exit(EXIT_FAILURE);
    }
    ent = readdir(dir);
    while (ent != NULL) {
	char file_path[file_path_size] = static_folder;
	printf("found %s inside static/ \n", ent->d_name);

	if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0){
	    strncat(file_path, ent->d_name, file_path_size);

	    int file_fd = open(file_path, O_RDONLY);
	    FILE *fp = fopen(file_path, "r");

	    if (fp == NULL || file_fd == -1) {
		perror("could not open file");
		exit(EXIT_FAILURE);
	    }

	    char file_ext[32];
	    strcpy(file_ext, get_file_extension(ent->d_name));
	    const char *mime_type = get_mime_type(file_ext);
	    snprintf(memo[memo_index].response, buff_size, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", mime_type);

	    struct stat file_stat;
	    fstat(file_fd, &file_stat);
	    off_t file_size = file_stat.st_size;

	    fread(memo[memo_index].response+strlen(memo[memo_index].response), sizeof(char), file_size, fp);
	    strncpy(memo[memo_index].file_name, ent->d_name, 10);
	    memo[memo_index].length = strlen(memo[memo_index].response);
	    if (memo_index < 5) { memo_index++; }

	    close(file_fd);
	    fclose(fp);
	}

	ent = readdir(dir);
    }

    closedir(dir);
}

int main(int argc, char *argv[]) {
    // initialization
    short reg_status = regcomp(&regex, "^GET /([^ ]*) HTTP/1", REG_EXTENDED);
    exit_in_failure(reg_status, "regex compilation failed");
    load_memo();
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
    #if is_debug_enable
	gettimeofday(&t0, NULL);
    #endif

    int client_fd = *((int *)arg);
    char *buffer = (char *)malloc(buff_size * sizeof(char));

    regmatch_t matches[2];
    ssize_t bytes_received = recv(client_fd, buffer, buff_size, 0);
    short regexec_status = regexec(&regex, buffer, 2, matches, 0);

    if(bytes_received > 0 && regexec_status == 0) {
	#if is_debug_enable
	    printf("buffer: %s\n", buffer);
	    printf("buffer size: %ld\n", strlen(buffer));
	#endif

	buffer[matches[1].rm_eo] = '\0';
	char *file_name = buffer + matches[1].rm_so;

	#if is_debug_enable
	    printf("file name: %s\n",file_name);
	#endif

	if (file_name[0] == '\0')
	    send(client_fd, memo[1].response, memo[1].length, 0);
	else
	    send(client_fd, memo[0].response, memo[0].length, 0);
    }

    close(client_fd);
    free(arg);
    free(buffer);

    #if is_debug_enable
	gettimeofday(&t1, NULL);
	printf("Did it in: %.2g seconds\n", t1.tv_sec - t0.tv_sec + 1E-6 * (t1.tv_usec - t0.tv_usec));
    #endif

    return NULL;
}
