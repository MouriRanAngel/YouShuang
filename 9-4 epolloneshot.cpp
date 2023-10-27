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
#include <sys/epoll.h>
#include <pthread.h>

const int MAX_EVENT_NUMBER = 1024;
const int BUFFER_SIZE = 1024;

struct fds {

    int epollfd;
    int sockfd;
};

int setnonblocking(int fd) {

    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;

    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// Register the EPOLLIN and EPOLLET events on fd into the epoll kernel event table indicated by epollfd. 
// The parameter 'oneshot' specifies whether to register the EPOLLONESHOT event on fd.
void addfd(int epollfd, int fd, bool oneshot) {

    epoll_event event;

    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;

    if (oneshot) {

        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    setnonblocking(fd);
}

// Reset events on fd. After doing this, although the EPOLLONESHOT event on fd is registered, 
// the operating system will still trigger the EPOLLIN event on fd, and only trigger it once.
void reset_oneshot(int epollfd, int fd) {

    epoll_event event;

    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// Worker thread.
void* worker(void* arg) {

    int sockfd = ((fds*) arg) -> sockfd;
    int epollfd = ((fds*) arg) -> epollfd;

    printf("start new thread to receive data on fd: %d\n", sockfd);

    char buf[BUFFER_SIZE];
    memset(buf, '\0', BUFFER_SIZE);

    // Loop through the data on sockfd until an EAGAIN error is encountered.
    while (1) {

        int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);

        if (ret == 0) {

            close(sockfd);

            printf("foreiner closed the connection\n");
            break;
        }
        else if (ret < 0) {

            if (errno == EAGAIN) {

                reset_oneshot(epollfd, sockfd);

                printf("read later\n");
                break;
            }
        }
        else {

            printf("get content: %s\n", buf);

            // Sleep for 5 seconds to simulate the data processing process.
            sleep(5);
        }
    }

    printf("end thread receiving data on fd: %d\n", sockfd);
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

    epoll_event events[MAX_EVENT_NUMBER];

    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    // Note that the EPOLLONESHOT event cannot be registered on the listening socket listenfd,
    // otherwise the application can only handle one customer connection! 
    // Because subsequent client connection requests will no longer trigger the EPOLLIN event on listenfd.
    addfd(epollfd, listenfd, false);

    while (1) {

        int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if (ret < 0) {

            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < ret; ++i) {

            int sockfd = events[i].data.fd;

            if (sockfd == listenfd) {

                struct sockaddr_in client_address;
                socklen_t client_addresslength = sizeof(client_address);

                int connfd = accept(listenfd, (struct sockaddr*)& client_address, &client_addresslength);

                // Register the EPOLLONESHOT event for each non-listening file descriptor.
                addfd(epollfd, connfd, true);
            }
            else if (events[i].events & EPOLLIN) {

                pthread_t thread;

                fds fds_for_new_worker;

                fds_for_new_worker.epollfd = epollfd;
                fds_for_new_worker.sockfd = sockfd;

                // Start a new worker thread to serve sockfd.
                pthread_create(&thread, nullptr, worker, (void*)& fds_for_new_worker);
            }
            else {

                printf("something else happened\n");
            }
        }
    }

    close(listenfd);
    return 0;
}