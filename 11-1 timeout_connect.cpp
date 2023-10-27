#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

// timeout connection function.
int timeout_connect(const char* ip, int port, int time) {

    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_family);
    address.sin_port = htons(port);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sockfd >= 0);

    // The type of the timeout set by the options SO_RCVTIMEO and SO_SNDTIMEO is timeval, 
    // which is the same type as the timeout parameter of the select system call.
    struct timeval timeout;

    timeout.tv_sec = time;
    timeout.tv_usec = 0;

    socklen_t len = sizeof(timeout);

    ret = setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, len);
    assert(ret != -1);

    ret = connect(sockfd, (struct sockaddr*)& address, sizeof(address));

    if (ret == -1) {

        // The error number corresponding to the timeout is EINPROGRESS. 
        // If the following conditions are true, we can process scheduled tasks.
        if (errno == EINPROGRESS) {

            printf("connecting timeout, process timeout logic\n");
            return -1;
        }

        printf("error occur when connecting to server\n");
        return -1;
    }

    return sockfd;
}

int main(int argc, char* argv[]) 
{
    if (argc <= 2) {

        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    int sockfd = timeout_connect(ip, port, 10);

    if (sockfd < 0) return 1;

    return 0;
}