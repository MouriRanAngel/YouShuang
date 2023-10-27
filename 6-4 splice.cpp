#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

int main(int argc, char* argv[])
{
    if (argc <= 2) {

        printf("usage: %s ip_address port_number.\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

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
    socklen_t client_addresslength = sizeof(client);

    int connfd = accept(sock, (struct sockaddr*)& client, &client_addresslength);

    if (connfd < 0) {

        printf("errno is: %d.\n", errno);
    }
    else {

        int pipefd[2];
        assert(ret != -1);

        ret = pipe(pipefd); // Create pipeline

        // Direct incoming customer data on connfd into a pipeline.
        ret = splice(connfd, nullptr, pipefd[1], nullptr, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        assert(ret != -1);

        // Direct the output of the pipe to the connfd client connection file descriptor.
        ret = splice(pipefd[0], nullptr, connfd, nullptr, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        assert(ret != -1);

        close(connfd);
    }

    close(sock);
    return 0;
}