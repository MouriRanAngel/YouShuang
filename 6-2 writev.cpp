#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024

// Define two HTTP status codes and status information.
static const char* status_line[2] = {"200 OK", "500 Internal server error"};

int main(int argc, char* argv[]) 
{
    if (argc <= 3) {

        printf("usage: %s ip_address port_number filename.\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    // Pass the target file as the third parameter of the program.
    const char* file_name = argv[3];

    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    int ret = bind(sock, (struct sockaddr*)& address, sizeof(address));
    assert(ret != -1);

    ret = listen(sock, 5);
    assert(ret != -1);

    struct sockaddr_in client;
    socklen_t client_addrlength = sizeof(client);

    int connfd = accept(sock, (struct sockaddr*)& client, &client_addrlength);

    if (connfd < 0) {

        printf("errno is: %d.\n", errno);
    }
    else {

        // A buffer used to hold the status line, header fields, and an empty line of the HTTP response.
        char header_buf[BUFFER_SIZE];
        memset(header_buf, '\0', BUFFER_SIZE);

        // Application cache for storing the contents of target files.
        char* file_buf;

        // Used to obtain the attributes of the target file, such as whether it is a directory, file size, etc.
        struct stat file_stat;

        // Record whether the target file is a valid file.
        bool valid = true;

        // How many bytes of space the buffer header_buf has currently used.
        int len = 0;

        if (stat(file_name, &file_stat) < 0) {

            // Target file does not exist.
            valid = false;
        }
        else {

            if (S_ISDIR(file_stat.st_mode)) {

                // The target file is a directory.
                valid = false;
            }
            else if (file_stat.st_mode & S_IROTH) {

                // The current user has permission to read the target file.

                // Dynamically allocate the buffer file_buf, and specify its size as the target file file_stat.st_size plus 1, 
                // and then read the target file into the buffer file_buf.
                int fd = open(file_name, O_RDONLY);

                file_buf = new char[file_stat.st_size + 1];
                memset(file_buf, '\0', file_stat.st_size + 1);

                if (read(fd, file_buf, file_stat.st_size) < 0) {

                    valid = false;
                }
            }
            else {

                valid = false;
            }
        }

        // If the target file is valid, send a normal HTTP response.
        if (valid) {

            // The following part adds the status line of the HTTP response, 
            // the "Content-Length" header field and a blank line to header_buf in sequence.
            ret = snprintf(header_buf, BUFFER_SIZE - 1, "%s%s\r\n", "HTTP/1.1", status_line[0]);
            len += ret;
            ret = snprintf(header_buf + len, BUFFER_SIZE - 1 - len, "Content-Length:%ld\r\n", file_stat.st_size);
            len += ret;
            ret = snprintf(header_buf + len, BUFFER_SIZE - 1 - len, "%s", "\r\n");

            // Use writev to write out the contents of header_buf and file_buf together.
            struct iovec iv[2];

            iv[0].iov_base = header_buf;
            iv[0].iov_len = strlen(header_buf);
            iv[1].iov_base = file_buf;
            iv[1].iov_len = file_stat.st_size;

            ret = writev(connfd, iv, 2);
        }
        else {

            // If the target file is invalid, the client is notified that an "internal error" has occurred in the server.
            ret = snprintf(header_buf, BUFFER_SIZE - 1, "%s%s\r\n", "HTTP/1.1", status_line[1]);
            len += ret;
            ret = snprintf(header_buf + len, BUFFER_SIZE - 1 - len, "%s", "\r\n");

            send(connfd, header_buf, strlen(header_buf), 0);
        }

        close(connfd);
        delete[] file_buf;
    }

    close(sock);
    return 0;
}