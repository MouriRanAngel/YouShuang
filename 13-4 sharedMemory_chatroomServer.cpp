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
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#define USER_LIMIT 5
#define BUFFER_SIZE 1024
#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define PROCESS_LIMIT 65536

// Process data necessary for a client connection.
struct client_data {

    sockaddr_in address;  // Client’s socket address.
    int connfd;           // socket file descriptor.
    pid_t pid;            // The PID of the child process handling this connection.
    int pipefd[2];        // The pipe used to communicate with the parent process.
};

static const char* shm_name = "/my_shm";

int sig_pipefd[2];
int epollfd;
int listenfd;
int shmfd;
char* share_mem = 0;

// Client connection array.
// The process uses the customer connection number to index
// this array to obtain relevant customer connection data.
client_data* users = 0;

// Mapping relationship table between child processes and client connections.
// By indexing this array with the PID of the process,
// you can obtain the number of the client connection handled by the process.
int* sub_process = 0;

// Current number of customers.
int user_count = 0;

bool stop_child = false;

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

    send(sig_pipefd[1], (char*)& msg, 1, 0);  

    errno = save_errno;
}

void addsig(int sig, void (*handler)(int), bool restart = true) {

    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    sa.sa_handler = handler;

    if (restart) {

        sa.sa_flags |= SA_RESTART;
    }
    
    sigfillset(&sa.sa_mask);

    assert(sigaction(sig, &sa, nullptr) != -1);
}

void del_resource() {

    close(sig_pipefd[0]);
    close(sig_pipefd[1]);

    close(epollfd);
    close(listenfd);

    shm_unlink(shm_name);

    delete[] users;
    delete[] sub_process;
}

// Stop a child process.
void child_term_handler(int sig) {

    stop_child = true;
}

// The function run by the child process.
// The parameter idx indicates the number of customer connections processed by the sub-process,
// users is an array that saves all customer connection data,
// and the parameter share_mem indicates the starting address of the shared memory.
int run_child(int idx, client_data* users, char* share_mem) {

    epoll_event events[MAX_EVENT_NUMBER];

    // The child process uses I/O multiplexing technology to listen to two file descriptors
    // at the same time: the client connection socket and the pipe file descriptor communicating with the parent process.
    int child_epollfd = epoll_create(5);
    assert(child_epollfd != -1);

    int connfd = users[idx].connfd;
    addfd(child_epollfd, connfd);

    int pipefd = users[idx].pipefd[1];
    addfd(child_epollfd, pipefd);

    int ret;

    // The child process needs to set its own signal processing function.
    addsig(SIGTERM, child_term_handler, false);

    while (!stop_child) {

        int number = epoll_wait(child_epollfd, events, MAX_EVENT_NUMBER, -1);

        if ((number < 0) && (errno != EINTR)) {

            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i) {

            int sockfd = events[i].data.fd;

            // The client connection responsible for this sub-process has data arriving.
            if ((sockfd == connfd) && (events[i].events & EPOLLIN)) {

                memset(share_mem + idx * BUFFER_SIZE, '\0', BUFFER_SIZE);

                // Read customer data into the corresponding read cache.
                // The read cache is a section of shared memory that starts at idx*BUFFER_SIZE and is BUFFER_SIZE bytes long.
                // Therefore, the read cache of each client connection is shared.
                ret = recv(connfd, share_mem + idx * BUFFER_SIZE, BUFFER_SIZE - 1, 0);

                if (ret < 0) {
                    
                    if (errno != EAGAIN) {
                        
                        stop_child = true;
                    }
                }
                else if (ret == 0) {

                    stop_child = true;
                }
                else {

                    // After successfully reading the customer data,
                    // the main process is notified (through a pipe) for processing.
                    send(pipefd, (char*)& idx, sizeof(idx), 0);
                }
            }  
            // The main process notifies this process (through a pipe) to send the client's data to the client responsible for this process.
            else if ((sockfd == pipefd) && (events[i].events & EPOLLIN)) {

                int client = 0;

                // Receive the data sent by the main process, that is, the number of the connection where the customer data arrives.
                ret = recv(sockfd, (char*)& client, sizeof(client), 0);

                if (ret < 0) {

                    if (errno != EAGAIN) {

                        stop_child = true;
                    }
                }
                else if (ret == 0) {

                    stop_child = true;
                }
                else {

                    send(connfd, share_mem + client * BUFFER_SIZE, BUFFER_SIZE, 0);
                }
            }     
            else {

                continue;
            }
        }
    }

    close(connfd);
    close(pipefd);
    close(child_epollfd);

    return 0;
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

    user_count = 0;

    users = new client_data[USER_LIMIT + 1];
    sub_process = new int[PROCESS_LIMIT];

    for (int i = 0; i < PROCESS_LIMIT; ++i) {

        sub_process[i] = -1;
    }

    epoll_event events[MAX_EVENT_NUMBER];

    epollfd = epoll_create(5);
    assert(epollfd != -1);

    addfd(epollfd, listenfd);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);

    setnonblocking(sig_pipefd[1]);

    addfd(epollfd, sig_pipefd[0]);

    addsig(SIGCHLD, sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT, sig_handler);
    addsig(SIGPIPE, SIG_IGN);

    bool stop_server = false;
    bool terminate = false;

    // Create shared memory as a read cache for all client socket connections.
    shmfd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    assert(shmfd != -1);

    ret = ftruncate(shmfd, USER_LIMIT * BUFFER_SIZE);
    assert(ret != -1);

    share_mem = (char*)mmap(nullptr, USER_LIMIT * BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    assert(share_mem != MAP_FAILED);

    close(shmfd);

    while (!stop_server) {

        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if ((number < 0) && (errno != EINTR)) {

            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i) {

            int sockfd = events[i].data.fd;

            // new client connections arrive.
            if (sockfd == listenfd) {

                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);

                int connfd = accept(listenfd, (struct sockaddr*)& client_address, &client_addrlength);

                if (connfd < 0) {

                    printf("errno is: %d\n", errno);
                    continue;
                }

                if (user_count >= USER_LIMIT) {

                    const char* info = "too many users\n";
                    printf("%s", info);

                    send(connfd, info, strlen(info), 0);

                    close(connfd);
                    continue;
                }

                // Save data related to the 'user_count' customer connection.
                users[user_count].address = client_address;
                users[user_count].connfd = connfd;

                // Establish a pipeline between the main process and the child process to pass necessary data.
                ret = socketpair(PF_UNIX, SOCK_STREAM, 0, users[user_count].pipefd);
                assert(ret != -1);

                pid_t pid = fork();

                if (pid < 0) {

                    close(connfd);
                    continue;
                }
                else if (pid == 0) {

                    close(epollfd);
                    close(listenfd);
                    close(users[user_count].pipefd[0]);
                    close(sig_pipefd[0]);
                    close(sig_pipefd[1]);

                    run_child(user_count, users, share_mem);

                    munmap((void*)share_mem, USER_LIMIT * BUFFER_SIZE);
                    exit(0);
                }
                else {

                    close(connfd);
                    close(users[user_count].pipefd[1]);

                    addfd(epollfd, users[user_count].pipefd[0]);
                    users[user_count].pid = pid;

                    // Record the index value of the new customer connection in the users array,
                    // and establish a mapping relationship between the process pid and the index value.
                    sub_process[pid] = user_count;
                    ++user_count;
                }
            }
            // handle signal events.
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)) {

                int sig;
                char signals[1024];

                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);

                if (ret == -1 or ret == 0) {

                    continue;
                }
                else {

                    for (int i = 0; i < ret; ++i) {

                        switch (signals[i]) {

                            // The child process exits, indicating that a client has closed the connection.
                            case SIGCHLD: {

                                pid_t pid;
                                int stat;

                                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {

                                    // Get the number of the closed client connection using the pid of the child process.
                                    int del_user = sub_process[pid];
                                    sub_process[pid] = -1;

                                    if ((del_user < 0) or (del_user > USER_LIMIT)) continue;

                                    // Clear related data used by the 'del_user' customer connection.
                                    epoll_ctl(epollfd, EPOLL_CTL_DEL, users[user_count].pipefd[0], 0);

                                    close(users[del_user].pipefd[0]);

                                    users[del_user] = users[--user_count];

                                    sub_process[users[del_user].pid] = del_user;
                                }

                                if (terminate && user_count == 0) {

                                    stop_server = true;
                                }

                                break;
                            }
                            case SIGTERM:
                            case SIGINT: {

                                // End server program.
                                printf("kill all the child now\n");

                                if (user_count == 0) {

                                    stop_server = true;
                                    break;
                                }

                                for (int i = 0; i < user_count; ++i) {

                                    int pid = users[i].pid;
                                    kill(pid, SIGTERM);
                                }

                                terminate = true;
                                break;
                            }
                            default: {

                                break;
                            }
                        }
                    }
                }
            }
            // A child process writes data to the parent process.
            else if (events[i].events & EPOLLIN) {

                int child = 0;

                // Read the pipeline data. The child variable records which client connection has the data arriving.
                ret = recv(sockfd, (char*)& child, sizeof(child), 0);
                printf("read data from child accross pipe\n");

                if (ret == -1 or ret == 0) {

                    continue;
                } 
                else {

                    // Send messages to other child processes except the child process responsible for handling 
                    // the child client connection, informing them that there is customer data to be written.
                    for (int j = 0; j < user_count; ++j) {

                        if (users[j].pipefd[0] != sockfd) {

                            printf("send data to child accross pipe\n");

                            send(users[j].pipefd[0], (char*)& child, sizeof(child), 0);
                        }
                    }
                }
            }
        }
    }

    del_resource();
    
    return 0;
}