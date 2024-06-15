#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

int get_index_of_substring(char* source, char* substring) {
    // return the index of the first substring if found,
    // else return -1
    int srcLen = strlen(source);
    int subStrLen = strlen(substring);
    char* sourceCopy = calloc(srcLen, sizeof(char));
    strcpy(sourceCopy, source);
    int count = 0;
    while (sourceCopy[0] != '\0'){
        if (strncmp(sourceCopy, substring, subStrLen) == 0) return count;
        else {
            count++;
            sourceCopy++;
        }
    }
    return -1;
}

int main() {
	// Disable output buffering
	setbuf(stdout, NULL);
 	setbuf(stderr, NULL);

	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;
    char buffer[BUFFER_SIZE] = {0};
	
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	
	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	
	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(4221),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
	
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}
	
	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}
	
	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);
	
	int client = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (read(client, buffer, BUFFER_SIZE) < 0) perror("read");
    
        printf("Client connected\n");

    int command = get_index_of_substring(buffer, "GET");
    if (command >= 0) {
        if (buffer[4] == '/' && buffer[5] == ' ') {
            char* reply = "HTTP/1.1 200 OK\r\n\r\n";
            send(client, reply, strlen(reply), 0);
        }
        else {
            char* reply = "HTTP/1.1 404 Not Found\r\n\r\n";
            send(client, reply, strlen(reply), 0);
        }
    }

    //printf("buffer = %s\n", buffer);

	
	close(server_fd);

	return 0;
}
