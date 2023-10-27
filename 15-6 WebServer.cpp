#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "14-2 locker.h"
#include "15-3 threadpool.h"
#include "15-4 http_conn.h"

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;

extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);

void addsig(int sig, void(handler)(int), bool restart = true) {

    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    sa.sa_handler = handler;

    if (restart) {

        sa.sa_flags |= SA_RESTART;
    }

    sigfillset(&sa.sa_mask);

    assert(sigaction(sig, &sa, nullptr) != -1);
}

void show_error(int connfd, const char* info) {

    printf("%s", info);

    send(connfd, info, strlen(info), 0);

    close(connfd);
}

int main(int argc, char* argv[])
{
    if (argc <= 2) {

        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    // Ignore SIGPIPE signal.
    addsig(SIGPIPE, SIG_IGN);

    // Create thread pool.
    threadpool<http_conn>* pool = nullptr;

    try {

        pool = new threadpool<http_conn>;
    }
    catch (...) {

        return 1;
    }

    // Pre-allocate an http_conn object for each possible client connection.
    http_conn* users = new http_conn[MAX_FD];
    assert(users);

    int user_count = 0;

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    struct linger tmp = {1, 0};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr*)& address, sizeof(address));
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];

    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    addfd(epollfd, listenfd, false);

    http_conn::m_epollfd = epollfd;

    while (true) {

        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if ((number < 0) && (errno != EINTR)) {

            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i) {

            int sockfd = events[i].data.fd;

            if (sockfd == listenfd) {

                struct sockaddr_in client_address;
                socklen_t client_addresslength = sizeof(client_address);

                int connfd = accept(listenfd, (struct sockaddr*)& client_address, &client_addresslength);

                if (connfd < 0) {

                    printf("errno is: %d\n", errno);
                    continue;
                }

                if (http_conn::m_user_count >= MAX_FD) {

                    show_error(connfd, "Internal server busy");
                    continue;
                }

                // Initialize client connection.
                users[connfd].init(connfd, client_address);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {

                // If there is an exception, directly close the customer connection.
                users[sockfd].close_conn();
            }
            else if (events[i].events & EPOLLIN) {

                // Based on the read results, decide whether to add the task to the thread pool or close the connection.
                if (users[sockfd].read()) {

                    pool->append(users + sockfd);
                }
                else {

                    users[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT) {

                // Based on the result of writing, decide whether to close the connection.
                if (!users[sockfd].write()) {

                    users[sockfd].close_conn();
                }
            }
            else {

                // nothing at all.
            }
        }

        close(epollfd);
        close(listenfd);

        delete[] users;
        delete pool;

        return 0;
    }
}