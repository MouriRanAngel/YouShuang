#define _GNU_SOURCE 1

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <poll.h>

const int USER_LIMIT = 5;    // Maximum number of users.
const int BUFFER_SIZE = 64;  // Read buffer size.
const int FD_LIMIT = 65535;  // File descriptor limit.

// Customer data: client socket address, location of data to be written to the client, data read from the client.
struct client_data {

    sockaddr_in address;
    char* write_buf;
    char buf[BUFFER_SIZE];
};

int setnonblocking(int fd) {

    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;

    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

int main(int argc, char* argv[]) 
{
    if (argc <= 2) {

        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    ret = bind(listenfd, (struct sockaddr*)& address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    // Create a users array and allocate FD_LIMIT client_data objects. 
    // It can be expected that each possible socket connection can obtain one such object, 
    // and the value of the socket can be directly used to index (as the subscript of the array) 
    // the client_data object corresponding to the socket connection. 
    // This is a simple way to associate the socket with customer data. and efficient way.
    client_data* users = new client_data[FD_LIMIT];

    // Although we have allocated enough client_data objects, in order to improve the performance of poll, 
    // it is still necessary to limit the number of users.
    pollfd fds[USER_LIMIT + 1];

    int user_counter = 0;

    for (int i = 1; i <= USER_LIMIT; ++i) {

        fds[i].fd = -1;
        fds[i].events = 0;
    }

    fds[0].fd = listenfd;
    fds[0].events = POLLIN | POLLERR;
    fds[0].revents = 0;

    while (1) {

        ret = poll(fds, user_counter + 1, -1);

        if (ret < 0) {

            printf("poll failure\n");
            break;
        }

        for (int i = 0; i < user_counter + 1; ++i) {

            if ((fds[i].fd == listenfd) && (fds[i].revents & POLLIN)) {

                struct sockaddr_in client_address;
                socklen_t client_addresslength = sizeof(client_address);

                int connfd = accept(listenfd, (sockaddr*)& client_address, &client_addresslength);

                if (connfd < 0) {

                    printf("errno is: %d\n", errno);
                    continue;
                }

                // If there are too many requests, close new connections.
                if (user_counter >= USER_LIMIT) {

                    const char* info = "too many users\n";
                    printf("%s", info);

                    send(connfd, info, strlen(info), 0);

                    close(connfd);
                    continue;
                }

                // For new connections, modify the fds and users arrays at the same time. 
                // As mentioned earlier, users[connfd] corresponds to the customer data of the new connection file descriptor connfd.
                ++user_counter;
                users[connfd].address = client_address;

                setnonblocking(connfd);

                fds[user_counter].fd = connfd;
                fds[user_counter].events = POLLIN | POLLRDHUP | POLLERR;
                fds[user_counter].revents = 0;

                printf("comes a new user, now have %d users\n", user_counter);
            }
            else if (fds[i].revents & POLLERR) {

                printf("get an error from %d\n", fds[i].fd);

                char errors[100];
                memset(errors, '\0', 100);
                socklen_t length = sizeof(errors);

                if (getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &errors, &length) < 0) {

                    printf("get socket option failed\n");
                }

                continue;
            }
            else if (fds[i].revents & POLLRDHUP) {

                // If the client closes the connection, the server also closes the corresponding connection
                // and decrements the total number of users by one.

                users[fds[i].fd] = users[fds[user_counter].fd];  // Copy the last user's information to the user who currently closes the connection.
                close(fds[i].fd);                                // Close the currently connected socket.

                fds[i] = fds[user_counter];                      // Copies the information of the last socket to the currently closed socket.


                // Decrease the values of i and user_counter so that the next socket is handled correctly in the next loop.
                --i; --user_counter;      

                printf("a client left\n");
            }
            else if (fds[i].revents & POLLIN) {

                int connfd = fds[i].fd;
                memset(users[connfd].buf, '\0', BUFFER_SIZE);

                ret = recv(connfd, users[connfd].buf, BUFFER_SIZE - 1, 0);

                printf("get %d bytes of client data %s from %d\n", ret, users[connfd].buf, connfd);

                if (ret < 0) {

                    // If an error occurs during the read operation, close the connection.
                    if (errno != EAGAIN) {

                        close(connfd);

                        users[fds[i].fd] = users[fds[user_counter].fd];
                        fds[i] = fds[user_counter];

                        --i;
                        --user_counter;
                    }
                }
                else if (ret == 0) {

                    // don't do anything.
                }
                else {

                    // If customer data is received, other socket connections are notified to prepare to write data.
                    for (int j = 1; j <= user_counter; ++j) {

                        if (fds[j].fd == connfd) continue;

                        fds[j].events |= ~POLLIN;
                        fds[j].events |= POLLOUT;

                        users[fds[j].fd].write_buf = users[connfd].buf;
                    }
                }
            }
            else if (fds[i].revents & POLLOUT) {

                int connfd = fds[i].fd;

                if (!users[connfd].write_buf) continue;

                ret = send(connfd, users[connfd].write_buf, strlen(users[connfd].write_buf), 0);

                users[connfd].write_buf = nullptr;

                // After writing the data, you need to re-register the readable event on fds[i].
                fds[i].events |= ~POLLOUT;
                fds[i].events |= POLLIN;
            }
        }
    }

    delete[] users;
    close(listenfd);
    
    return 0;
}