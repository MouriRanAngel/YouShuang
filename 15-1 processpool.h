#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

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
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

// A class that describes a child process. m_pid is the PID of the target child process,
// and m_pipefd is the pipe used to communicate between the parent process and the child process.
class process {
public:
    process() : m_pid(-1) {}
public:
    pid_t m_pid;
    int m_pipefd[2];
};

// Process pool class, defined as a template class for code reuse.
// Its template parameter is a class that handles logical tasks.
template<typename T>
class processpool {
private:
    // Define the constructor as private,
    // so we can only create processpool instances through the later create static function.
    processpool(int listenfd, int process_number = 8);

public:
    // Single mode to ensure that the program creates at most one processpool instance,
    // which is a necessary condition for the program to correctly handle signals.
    static processpool<T>* create(int listenfd, int process_number = 8) {

        if (!m_instance) {

            m_instance = new processpool<T>(listenfd, process_number);
        }

        return m_instance;
    }

    ~processpool() {

        delete[] m_sub_process;
    }

    void run();  // Start process pool.

private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    // The maximum number of child processes allowed in the process pool.
    static const int MAX_PROCESS_NUMBER = 16;

    // The maximum number of customers that each child process can handle.
    static const int USER_PRE_PROCESS = 65536;

    // The maximum number of events that epoll can handle.
    static const int MAX_EVENT_NUMBER = 10000;

    // The total number of processes in the process pool.
    int m_process_number;

    // The sequence number of the child process in the pool, starting from 0.
    int m_idx;

    // Each process has an epoll kernel event table, identified by m_epollfd.
    int m_epollfd;

    // Listening socket.
    int m_listenfd;

    // The child process uses m_stop to decide whether to stop running.
    int m_stop;

    // Save description information of all child processes.
    process* m_sub_process;

    // Process pool static instance.
    static processpool<T>* m_instance;
};

template<typename T>
processpool<T>* processpool<T>::m_instance = nullptr;

// Pipeline for processing signals to implement unified event sourcing. Hereafter called the signal pipeline.
static int sig_pipefd[2];

static int setnonblocking(int fd) {

    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;

    fcntl(fd, F_SETFL, new_option);

    return old_option;
}

static void addfd(int epollfd, int fd) {

    epoll_event event;

    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    setnonblocking(fd);
}

// Remove all registered events on fd from the epoll kernel event table identified by epollfd.
static void removefd(int epollfd, int fd) {

    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

static void sig_handler(int sig) {

    int save_errno = errno;
    int msg = sig;

    send(sig_pipefd[1], (char*)& msg, 1, 0);

    errno = save_errno;
}

static void addsig(int sig, void(handler)(int), bool restart = true) {

    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    sa.sa_handler = handler;

    if (restart) {

        sa.sa_flags |= SA_RESTART;
    }

    sigfillset(&sa.sa_mask);

    assert(sigaction(sig, &sa, nullptr) != -1);
}

// Process pool constructor. The parameter listenfd is the listening socket,
// which must be created before creating the process pool, otherwise the child process cannot directly reference it.
// The parameter process_number specifies the number of child processes in the process pool.
template<typename T>
processpool<T>::processpool(int listenfd, int process_number) : m_listenfd(listenfd), m_process_number(process_number), m_idx(-1), m_stop(false) {

    assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));

    m_sub_process = new process[process_number];
    assert(m_sub_process);

    // Create 'process_number' child processes and establish pipes between them and the parent process.
    for (int i = 0; i < process_number; ++i) {

        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
        assert(ret == 0);

        m_sub_process[i].m_pid = fork();
        assert(m_sub_process[i].m_pid >= 0);

        if (m_sub_process[i].m_pid > 0) {

            close(m_sub_process[i].m_pipefd[1]);

            continue;
        }
        else {

            close(m_sub_process[i].m_pipefd[0]);
            m_idx = i;

            break;
        }
    }
}

// unified event source.
template<typename T>
void processpool<T>::setup_sig_pipe() {

    // Create epoll event listening table and signal pipeline.
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);

    setnonblocking(sig_pipefd[1]);

    addfd(m_epollfd, sig_pipefd[0]);    

    // Set signal processing function.
    addsig(SIGCHLD, sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT, sig_handler);
    addsig(SIGPIPE, SIG_IGN);
}

// The m_idx value in the parent process is -1, and the m_idx value in the child process is greater than or equal to 0.
// Based on this, we determine whether the parent process code or the child process code is to be run next.
template<typename T>
void processpool<T>::run() {

    if (m_idx != -1) {

        run_child();
        return;
    }

    run_parent();
}

template<typename T>
void processpool<T>::run_child() {

    setup_sig_pipe();

    // Each child process finds the pipe to communicate with the parent process
    // through its sequence number value 'm_idx' in the process pool.
    int pipefd = m_sub_process[m_idx].m_pipefd[1];

    // The child process needs to listen to the pipe file descriptor pipefd,
    // because the parent process will use it to notify the child process to accept the new connection.
    addfd(m_epollfd, pipefd);

    epoll_event events[MAX_EVENT_NUMBER];

    T* users = new T[USER_PRE_PROCESS];
    assert(users);

    int number = 0;
    int ret = -1;

    while (!m_stop) {

        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);

        if ((number < 0) && (errno != EINTR)) {

            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i) {

            int sockfd = events[i].data.fd;

            if ((sockfd == pipefd) && (events[i].events & EPOLLIN)) {

                int client;

                // Read data from the pipe between the parent and child processes and save the result in the variable client.
                // If the read is successful, it means that a new customer connection has arrived.
                ret = recv(sockfd, (char*)& client, sizeof(client), 0);

                if (((ret < 0) && (errno != EAGAIN)) or ret == 0) {

                    continue;
                }
                else {

                    struct sockaddr_in client_address;
                    socklen_t client_addresslength = sizeof(client_address);

                    int connfd = accept(m_listenfd, (struct sockaddr*)& client_address, &client_addresslength);

                    if (connfd < 0) {

                        printf("errno is: %d\n", errno);
                        continue;
                    }

                    addfd(m_epollfd, connfd);

                    // Template class T must implement the init method to initialize a client connection.
                    // We directly use connfd to index logical processing objects (T type objects) to improve program efficiency.
                    users[connfd].init(m_epollfd, connfd, client_address);
                }
            }
            // The following handles the signals received by the child process.
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)) {

                int sig;
                char signals[1024];

                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);

                if (ret <= 0) {

                    continue;
                }
                else {

                    for (int i = 0; i < ret; ++i) {

                        switch (signals[i]) {
                            
                            case SIGCHLD: {

                                pid_t pid;
                                int stat;

                                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {

                                    continue;
                                }

                                break;
                            }
                            case SIGTERM:
                            case SIGINT: {

                                m_stop = true;
                                break;
                            }
                            default: {

                                break;
                            }
                        }
                    }
                }
            }
            // If it is other readable data, then it must be a customer request.
            // Call the process method of the logical processing object to process it.
            else if (events[i].events & EPOLLIN) {

                users[sockfd].process();
            }
            else {

                continue;
            }
        }
    }

    delete[] users;
    users = nullptr;

    close(pipefd);
    // close(m_listenfd); /*We comment out this sentence to remind readers that this file descriptor (see later) 
    // should be closed by the creator of m_listenfd, which is the so-called "object (such as a file descriptor, 
    // and or A section of heap memory) should be destroyed by which function it is created."*/
    close(m_epollfd);
}

template<typename T>
void processpool<T>::run_parent() {

    setup_sig_pipe();

    // The parent process listens to m_listenfd.
    addfd(m_epollfd, m_listenfd);

    epoll_event events[MAX_EVENT_NUMBER];

    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while (!m_stop) {

        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);

        if ((number < 0) && (errno != EINTR)) {

            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i) {

            int sockfd = events[i].data.fd;

            if (sockfd == m_listenfd) {

                // If a new connection arrives, it is assigned to a sub-process using the Round Robin method.
                int i = sub_process_counter;

                do {

                    if (m_sub_process[i].m_pid != -1) break;

                    i = (i + 1) % m_process_number;
                }
                while (i != sub_process_counter);

                if (m_sub_process[i].m_pid == -1) {

                    m_stop = true;
                    break;
                }

                sub_process_counter = (i + 1) % m_process_number;

                send(m_sub_process[i].m_pipefd[0], (char*)& new_conn, sizeof(new_conn), 0);

                printf("send request to child %d\n", i);
            }
            // The following handles the signal received by the parent process.
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)) {

                int sig;
                char signals[1024];

                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);

                if (ret <= 0) {

                    continue;
                }
                else {

                    for (int i = 0; i < ret; ++i) {

                        switch (signals[i]) {

                            case SIGCHLD: {

                                pid_t pid;
                                int stat;

                                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {

                                    for (int i = 0; i < m_process_number; ++i) {

                                        // If the i-th sub-process in the process pool exits,
                                        // the main process closes the corresponding communication pipe and
                                        // sets the corresponding m_pid to -1 to mark that the sub-process has exited.
                                        if (m_sub_process[i].m_pid == pid) {

                                            printf("child %d join\n", i);

                                            close(m_sub_process[i].m_pipefd[0]);

                                            m_sub_process[i].m_pid = -1;
                                        }
                                    }
                                }

                                // If all child processes have exited, the parent process also exits.
                                m_stop = true;

                                for (int i = 0; i < m_process_number; ++i) {

                                    if (m_sub_process[i].m_pid != -1) {

                                        m_stop = false;
                                    }
                                }

                                break;
                            }
                            case SIGTERM:
                            case SIGINT: {

                                // If the parent process receives a termination signal, it kills all child processes and waits for them all to finish.
                                // Of course, a better way to notify the end of the child process is to send special data to the communication channel
                                // between the parent and child processes. Readers may wish to implement this by themselves.
                                printf("kill all the child now\n");

                                for (int i = 0; i < m_process_number; ++i) {

                                    int pid = m_sub_process[i].m_pid;

                                    if (pid != -1) {

                                        kill(pid, SIGTERM);
                                    }
                                }

                                break;
                            }
                            default: {

                                break;
                            }
                        }
                    }
                }
            }
            else {

                continue;
            }
        }
    }

    //close(m_listenfd); /*The file descriptor is closed by the creator (see below)*/
    close(m_epollfd);
}

#endif