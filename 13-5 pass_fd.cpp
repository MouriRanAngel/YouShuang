#include <sys/socket.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static const int CONTROL_LEN = CMSG_LEN(sizeof(int));

// Send a file descriptor.
// The fd parameter is the UNIX domain socket used to transmit information.
// The fd_to_send parameter is the file descriptor to be sent.
void send_fd(int fd, int fd_to_send) {

    struct iovec iov[1];
    struct msghdr msg;

    char buf[0];

    iov[0].iov_base = buf;
    iov[0].iov_len = 1;

    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    cmsghdr cm;

    cm.cmsg_len = CONTROL_LEN;
    cm.cmsg_level = SOL_SOCKET;
    cm.cmsg_type = SCM_RIGHTS;

    *(int*) CMSG_DATA(&cm) = fd_to_send;

    msg.msg_control = &cm;  // set auxiliary data.
    msg.msg_controllen = CONTROL_LEN;

    sendmsg(fd, &msg, 0);
}

// Receive target file descriptor.
int recv_fd(int fd) {

    struct iovec iov[1];
    struct msghdr msg;

    char buf[0];

    iov[0].iov_base = buf;
    iov[0].iov_len = 1;

    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    cmsghdr cm;

    msg.msg_control = &cm;
    msg.msg_controllen = CONTROL_LEN;

    recvmsg(fd, &msg, 0);

    int fd_to_send = *(int*) CMSG_DATA(&cm);

    return fd_to_send;
}

int main()
{
    int pipefd[2];
    int fd_to_pass = 0;

    // Create a pipe between the parent and child processes.
    // The file descriptors pipefd[0] and pipefd[1] are both UNIX domain socket.
    int ret = socketpair(PF_UNIX, SOCK_DGRAM, 0, pipefd);
    assert(ret != -1);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {

        close(pipefd[0]);

        fd_to_pass = open("test.txt", O_RDWR, 0666);

        // Create a pipe between the parent and child processes.
        // The file descriptors pipefd[0] and pipefd[1] are both UNIX domain sockets.
        send_fd(pipefd[1], (fd_to_pass > 0) ? fd_to_pass : 0);

        close(fd_to_pass);
        exit(0);
    }

    close(pipefd[1]);

    fd_to_pass = recv_fd(pipefd[0]);  // The parent process receives the target file descriptor from the pipe.

    char buf[1024];
    memset(buf, '\0', 1024);

    read(fd_to_pass, buf, 1024);  // Read the target file descriptor to verify its validity.

    printf("I got fd %d and data %s\n", fd_to_pass, buf);

    close(fd_to_pass);
}