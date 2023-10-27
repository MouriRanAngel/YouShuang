#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>
#include "11-2 lst_timer.h"

const int FD_LIMIT = 65535;
const int MAX_EVENT_NUMBER = 1024;
const int TIMESLOT = 5;

static int pipefd[2];

// Use the ascending linked list in code listing 11-2 to manage timers.
static sort_timer_lst timer_lst;
static int epollfd = 0;

int setnonblocking(int fd) {

    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;

    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd) {

    epoll_event event;

    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    setnonblocking(fd);
}

void sig_handler(int sig) {

    int save_errno = errno;
    int msg = sig;

    send(pipefd[1], (char*)& msg, 1, 0);  

    errno = save_errno;
}

void addsig(int sig) {

    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;

    sigfillset(&sa.sa_mask);

    assert(sigaction(sig, &sa, nullptr) != -1);
}

void timer_handler() {

    // Timing task processing is actually calling the tick function.
    timer_lst.tick();

    // Because an alarm call will only cause the SIGALRM signal once,
    // we need to retime it to continuously trigger the SIGALRM signal.
    alarm(TIMESLOT);
}

// Timer callback function,
// which deletes the registered events on the inactive connection socket and closes it.
void cb_func(client_data* user_data) {

    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);

    close(user_data->sockfd);

    printf("close fd %d\n", user_data->sockfd);
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

    addfd(epollfd, listenfd);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);

    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0]);

    // Set signal processing functions.
    addsig(SIGALRM);
    addsig(SIGTERM);

    bool timeout = false;
    bool stop_server = false;

    client_data* users = new client_data[FD_LIMIT];
    
    alarm(TIMESLOT);  // timing.

    while (!stop_server) {

        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if ((number < 0) && (errno != EINTR)) {

            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i) {

            int sockfd = events[i].data.fd;

            // Handle incoming client connections.
            if (sockfd == listenfd) {

                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);

                int connfd = accept(listenfd, (struct sockaddr*)& client_address, &client_addrlength);

                addfd(epollfd, connfd);

                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;

                // Create a timer, set its callback function and timeout, 
                // then bind the timer to user data,
                // and finally add the timer to the linked list 'timer_lst'.
                util_timer* timer = new util_timer;

                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;

                time_t cur = time(nullptr);
                timer->expire = cur + 3 * TIMESLOT;

                users[connfd].timer = timer;
                timer_lst.add_timer(timer);

                delete timer;
            }
            // Process signals.
            else if((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {

                int sig;
                char signals[1024];

                ret = recv(pipefd[0], signals, sizeof(signals), 0);

                if (ret == -1) {

                    // handle the error    
                    continue;
                }
                else if (ret == 0) {

                    continue;
                }
                else {

                    for (int i = 0; i < ret; ++i) {

                        switch (signals[i]) {

                            case SIGALRM: {
                                
                                // Use the 'timeout' variable to mark scheduled tasks that need to be processed, 
                                // but the scheduled tasks will not be processed immediately. 
                                // This is because the priority of scheduled tasks is not very high,
                                // and we prioritize other more important tasks.
                                timeout = true;
                                break;
                            }
                            case SIGTERM: {

                                stop_server = true;
                            }
                        }
                    }
                }
            }
            // Process data received on client connections.
            else if (events[i].events & EPOLLIN) {

                memset(users[sockfd].buf, '\0', BUFFER_SIZE);

                ret = recv(sockfd, users[sockfd].buf, BUFFER_SIZE - 1, 0);
                
                printf("get %d bytes of client data %s from %d\n", ret, users[sockfd].buf, sockfd);

                util_timer* timer = users[sockfd].timer;

                if (ret < 0) {

                    // If a read error occurs,
                    // close the connection and remove its corresponding timer.
                    if (errno != EAGAIN) {

                        cb_func(&users[sockfd]);

                        if (timer) {

                            timer_lst.del_timer(timer);
                        }
                    }
                }
                else if (ret == 0) {

                    // If the other party has closed the connection,
                    // we also close the connection and remove the corresponding timer.
                    cb_func(&users[sockfd]);

                    if (timer) {

                        timer_lst.del_timer(timer);
                    }
                }
                else {

                    // If there is readable data on a customer connection,
                    // we need to adjust the timer corresponding to the connection
                    // to delay the time when the connection is closed.
                    if (timer) {

                        time_t cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;

                        printf("adjust timer once\n");

                        timer_lst.adjust_timer(timer);
                    }
                }
            }
            else {
                
                // others.
            }
        }

        // Process timed events last because I/O events have higher priority.
        // Of course, doing so will result in the scheduled tasks not being executed exactly as expected.
        if (timeout) {

            timer_handler();
            timeout = false;
        }
    }

    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);

    delete[] users;
    return 0; 
}