#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <zlib.h>

#define BUFFER_SIZE 1024
#define HTTP "HTTP/1.1"

int get_index_of_substring(char* source, char* substring) {
    // return the index of the first substring if found,
    // else return -1
    int srcLen = strlen(source);
    int subStrLen = strlen(substring);
    char* sourceCopy = calloc(srcLen, sizeof(char));
    strcpy(sourceCopy, source);
    int count = 0;
    while (sourceCopy[0] != '\0'){
        if (strncmp(sourceCopy, substring, subStrLen) == 0) {
            //free(sourceCopy);
            return count;
        }
        else {
            count++;
            sourceCopy++;
        }
    }
    //free(sourceCopy);
    return -1;
}

char* gzip_compress(const char* data, int data_size, int* compressed_size) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    // Initialize zlib stream for compression
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return NULL;
    }

    zs.next_in = (Bytef*)data;
    zs.avail_in = data_size;

    int chunk = 16384; // Chunk size for output buffer
    char* out_buffer = (char*)malloc(chunk);
    if (!out_buffer) {
        deflateEnd(&zs);
        return NULL;
    }

    int total_compressed = 0;
    int ret;
    do {
        zs.next_out = (Bytef*)out_buffer + total_compressed;
        zs.avail_out = chunk - total_compressed;

        // Compress data
        ret = deflate(&zs, Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&zs);
            free(out_buffer);
            return NULL;
        }

        total_compressed = chunk - zs.avail_out;

        // Expand output buffer if needed
        if (zs.avail_out == 0) {
            chunk *= 2;
            out_buffer = (char*)realloc(out_buffer, chunk);
            if (!out_buffer) {
                deflateEnd(&zs);
                return NULL;
            }
        }
    } while (ret != Z_STREAM_END);

    *compressed_size = total_compressed;
    deflateEnd(&zs);

    return out_buffer;
}

int main(int argc, char** argv) {
    char* abs_path = "/";
    if (argc == 3) {
        if (strcmp(argv[1], "--directory") == 0) {
            abs_path = argv[2];
            DIR* check = opendir(abs_path);
            if (check) {
                closedir(check);
            }
            else {
                printf("Directory does not exists\n");
                return 1;
            }
        }
    }

	// Disable output buffering
	setbuf(stdout, NULL);
 	setbuf(stderr, NULL);

	int server_fd;
    socklen_t client_addr_len;
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
    while (1) {
        int client = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
        char* headers;
        char* body;

        if (read(client, buffer, BUFFER_SIZE) < 0) perror("read");
            printf("Client connected\n");
        printf("debug: Buffer:\n%s\n", buffer);


        // Copy the buffer to allow maniupulation without losing original request
        char* bufferCopy = calloc(BUFFER_SIZE, sizeof(char));
        strcpy(bufferCopy, buffer);

        int method = get_index_of_substring(buffer, "GET");
        if (method >= 0) {
            bufferCopy += 4;
            if (strncmp(bufferCopy, "/ ", 2) == 0) {
                headers = calloc(BUFFER_SIZE, sizeof(char));
                sprintf(headers, "%s 200 OK\r\n\r\n", HTTP);
                send(client, headers, strlen(headers), 0);
            }
            else if (strncmp(bufferCopy, "/echo/", 6) == 0) {
                bufferCopy += 6;
                int i = 0;
                for (; bufferCopy[i]; i++) {
                    if (bufferCopy[i] == ' ') break;
                }
                body = calloc(i + 1, sizeof(char));
                strncpy(body, bufferCopy, i);
                int compression = get_index_of_substring(bufferCopy, "Accept-Encoding: ");
                if (compression >= 0) {
                    bufferCopy += compression + 17;
                    int gzip = get_index_of_substring(bufferCopy, "gzip");
                    if (gzip >= 0) {
                        bufferCopy += gzip;
                        if (strncmp(bufferCopy, "gzip", 4) == 0) {
                            const char* const_body = body;
                            int body_size = strlen(const_body);
                            int compressed_size;
                            char* compressed_data = gzip_compress(const_body, body_size, &compressed_size);
                            if (compressed_size) {
                                headers = calloc(BUFFER_SIZE, sizeof(char));
                                sprintf(headers,
                                        "%s 200 OK\r\n"
                                        "Content-Encoding: gzip\r\n"
                                        "Content-Type: text/plain\r\n"
                                        "Content-Length: %d\r\n\r\n",
                                        HTTP, compressed_size);

                                send(client, headers, strlen(headers), 0);
                                send(client, compressed_data, compressed_size, 0);
                            }
                        }
                    }
                    else {
                        headers = calloc(BUFFER_SIZE, sizeof(char));
                        sprintf(headers,
                                "%s 200 OK\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: %i\r\n\r\n",
                                HTTP, i);
                        send(client, headers, strlen(headers), 0);
                        send(client, body, strlen(body), 0);
                    }
                }
                else {
                    headers = calloc(BUFFER_SIZE, sizeof(char));
                    sprintf(headers,
                            "%s 200 OK\r\n"
                            "Content-Type: text/plain\r\n"
                            "Content-Length: %i\r\n\r\n",
                            HTTP, i);
                    send(client, headers, strlen(headers), 0);
                    send(client, body, strlen(body), 0);
                }

            }
            else if (strncmp(bufferCopy, "/user-agent", 11) == 0) {
                bufferCopy += 11;
                int shift = get_index_of_substring(bufferCopy, "User-Agent: ");
                if (shift < 0) {
                    printf("User Agent failed: %s\n", strerror(errno));
                    return 1;
                }
                bufferCopy += shift + 12;
                int i = 0;
                for (; bufferCopy[i]; i++) {
                    if (bufferCopy[i] == '\r') break;
                }
                body = calloc(i + 1, sizeof(char));
                strncpy(body, bufferCopy, i);
                headers = calloc(BUFFER_SIZE, sizeof(char));
                sprintf(headers,
                        "%s 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: %i\r\n\r\n",
                        HTTP, i);
                send(client, headers, strlen(headers), 0);
                send(client, body, strlen(body), 0);
            }
            else if (strncmp(bufferCopy, "/files/", 7) == 0) {
                bufferCopy += 7;
                int i = 0;
                for (; bufferCopy[i]; i++) {
                    if (bufferCopy[i] == ' ') break;
                }
                char* tmpPath = calloc(i+1, sizeof(char));
                strncpy(tmpPath, bufferCopy, i);
                char* filepath = calloc(strlen(abs_path) + i + 1, sizeof(char));
                sprintf(filepath, "%s%s", abs_path, tmpPath);
                FILE* file = fopen(filepath, "r");
                if (file == NULL) {
                    headers = calloc(BUFFER_SIZE, sizeof(char));
                    sprintf(headers, "%s 404 Not Found\r\n\r\n", HTTP);
                    send(client, headers, strlen(headers), 0);
                }
                else {
                    fseek(file, 0, SEEK_END);
                    long file_size = ftell(file);
                    fseek(file, 0, SEEK_SET);
                    char* data = calloc(file_size, sizeof(char));
                    size_t read_size = fread(data, 1, file_size, file);
                    if (read_size != file_size) {
                        perror("Error reading file");
                        fclose(file);
                        return 1;
                    }
                    data[file_size] = '\0';
                    //printf("data: %s\n", data); 
                    fclose(file);
                    headers = calloc(BUFFER_SIZE+file_size, sizeof(char));
                    sprintf(headers,
                            "%s 200 OK\r\n"
                            "Content-Type: application/octet-stream\r\n"
                            "Content-Length: %i\r\n\r\n",
                        HTTP, (int) file_size);
                    send(client, headers, strlen(headers), 0);
                    send(client, data, strlen(data), 0);
                }

            }
            else {
                headers = calloc(BUFFER_SIZE, sizeof(char));
                sprintf(headers, "%s 404 Not Found\r\n\r\n", HTTP);
                send(client, headers, strlen(headers), 0);
            }
        }
        method = get_index_of_substring(buffer, "POST");
        if (method >= 0) {
            bufferCopy += 5;
            if (strncmp(bufferCopy, "/files/", 7) == 0) {
                bufferCopy+=7;
                int i = 0;
                for (; bufferCopy[i]; i++) if (bufferCopy[i] == ' ') break;

                char* tmpPath = calloc(i+1, sizeof(char));
                strncpy(tmpPath, bufferCopy, i);
                char* filename = calloc(strlen(abs_path) + i + 1, sizeof(char));
                sprintf(filename, "%s%s", abs_path, tmpPath);
                FILE* file = fopen(filename, "w");
                if (!file) {
                    printf("Error while oppening file\n");
                    fclose(file);
                    return 1;
                }

                int shift = get_index_of_substring(bufferCopy, "Content-Length: ");
                if (shift < 0) {
                    printf("error with the content Length of the POST request\n");
                    fclose(file);
                    return 1;
                }

                bufferCopy += shift + 16; // place pointer a the begining of the actual value


                i = 0;
                for (; buffer[i]; i++) if (buffer[i] == '\r') break;
                char* sLength = calloc(i+1, sizeof(char));
                strncpy(sLength, bufferCopy, i);
                int length = atoi(sLength);
                printf("lenght = %i\n", length);

                bufferCopy += 6;

                char* data = calloc(length, sizeof(char));
                strncpy(data, bufferCopy, length+1);
                fputs(data, file);
                fclose(file);
                headers = calloc(BUFFER_SIZE, sizeof(char));
                sprintf(headers, "%s 201 Created\r\n\r\n", HTTP);
                send(client, headers, strlen(headers), 0);
            }
            else {
                headers = calloc(BUFFER_SIZE, sizeof(char));
                sprintf(headers, "%s 404 Not Found\r\n\r\n", HTTP);
                send(client, headers, strlen(headers), 0);
            }
        }
    }
	close(server_fd);

	return 0;
}
