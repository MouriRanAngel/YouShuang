#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>

const int BUFFER_SIZE = 1023;

int setnonblocking(int fd) {

    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;

    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// Timeout connection function, the parameters are server IP address, port number and timeout time (milliseconds). 
// When the function succeeds, it returns the socket that is already connected, and when it fails, it returns -1.
int unblock_connect(const char* ip, int port, int time) {

    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sockfd >= 0);

    int fdopt = setnonblocking(sockfd);
    
    ret = connect(sockfd, (struct sockaddr*)& address, sizeof(address));

    if (ret == 0) {

        // If the connection is successful, restore the attributes of sockfd and return it immediately.
        printf("connect with server immediately\n");

        fcntl(sockfd, F_SETFL, fdopt);

        return sockfd;
    }
    else if (errno != EINPROGRESS) {

        // If the connection is not established immediately, then only when errno is EINPROGRESS, 
        // it means that the connection is still in progress, otherwise an error will be returned.
        printf("unblock connect not support\n");

        return -1;
    }

    fd_set readfds;
    fd_set writefds;

    struct timeval timeout;

    FD_ZERO(&readfds);
    FD_SET(sockfd, &writefds);

    timeout.tv_sec = time;
    timeout.tv_usec = 0;

    ret = select(sockfd + 1, nullptr, &writefds, nullptr, &timeout);

    if (ret <= 0) {

        // 'select' times out or an error occurs, return immediately.
        printf("connect time out\n");

        close(sockfd);
        return -1;
    }

    if (!FD_ISSET(sockfd, &writefds)) {

        printf("no events on sockfd found\n");

        close(sockfd);
        return -1;
    }

    int error = 0;
    socklen_t length = sizeof(error);

    // Call getsockopt to get and clear errors on sockfd.
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) {

        printf("get socket option failed\n");

        close(sockfd);
        return -1;
    }

    // If the error number is not 0, it means there is a connection error.
    if (error != 0) {

        printf("connection failed after select with the error: %d\n", error);

        close(sockfd);
        return -1;
    }

    // Connection succeeded.
    printf("connection ready after select with the socket: %d\n", sockfd);

    fcntl(sockfd, F_SETFL, fdopt);

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

    int sockfd = unblock_connect(ip, port, 10);

    if (sockfd < 0) {

        return 1;
    }

    close(sockfd);
    return 0;
}