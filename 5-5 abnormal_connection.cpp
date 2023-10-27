#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

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

    // Pause for 20 seconds to wait for client connection and 
    // related operations (dropping or exiting) to complete.
    sleep(20);

    struct sockaddr_in client;
    socklen_t client_addresslength = sizeof(client);

    int connfd = accept(sock, (struct sockaddr*)& client, &client_addresslength);

    if (connfd < 0) {

        printf("errno is: %d.\n", errno);
    }
    else {

        // If the connection is successfully received, 
        // the client's IP address and port number will be printed out.
        char remote[INET_ADDRSTRLEN];
        printf("connected with ip: %s and port: %d.\n", inet_ntop(AF_INET, &client.sin_addr, remote, INET_ADDRSTRLEN), ntohs(client.sin_port));

        close(connfd);
    }

    close(sock);
    return 0;
}