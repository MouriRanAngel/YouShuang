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
#include "15-1 processpool.h"

// A class used to handle client CGI requests.
// It can be used as a template parameter of the processpool class.
class cgi_conn {
public:
    cgi_conn() {}
    ~cgi_conn() {}

    // Initialize client connection and clear read buffer.
    void init(int epollfd, int sockfd, const sockaddr_in& client_addr) {

        m_epollfd = epollfd;
        m_sockfd = sockfd;
        m_address = client_addr;

        memset(m_buf, '\0', BUFFER_SIZE);

        m_read_idx = 0;
    }

    void process() {

        int idx = 0;
        int ret = -1;

        // Loop to read and analyze customer data.
        while (true) {

            idx = m_read_idx;

            ret = recv(m_sockfd, m_buf + idx, BUFFER_SIZE - 1 - idx, 0);

            // If an error occurs during the read operation, the client connection is closed.
            // But if there is no data to read temporarily, exit the loop.
            if (ret < 0) {

                if (errno != EAGAIN) {

                    removefd(m_epollfd, m_sockfd);
                }

                break;
            }
            // If the other party closes the connection, the server also closes the connection.
            else if (ret == 0) {

                removefd(m_epollfd, m_sockfd);
                break;
            }
            else {

                m_read_idx += ret;

                printf("user content is: %s\n", m_buf);

                // If the character "\r\n" is encountered, start processing the customer request.
                for (; idx < m_read_idx; ++idx) {

                    if ((idx >= 1) && (m_buf[idx - 1] == '\r') && (m_buf[idx] == '\n')) break;
                }

                // If the character "\r\n" is not encountered, more customer data needs to be read.
                if (idx == m_read_idx) continue;

                m_buf[idx - 1] = '\0';

                char* file_name = m_buf;

                // Determine whether the CGI program the customer wants to run exists.
                if (access(file_name, F_OK) == -1) {

                    removefd(m_epollfd, m_sockfd);
                    break;
                }

                // Create a child process to execute CGI programs.
                ret = fork();

                if (ret == -1) {

                    removefd(m_epollfd, m_sockfd);
                    break;
                }
                else if (ret > 0) {

                    // The parent process simply closes the connection.
                    removefd(m_epollfd, m_sockfd);
                    break;
                }
                else {

                    // The child process directs standard output to m_sockfd and executes the CGI program.
                    close(STDOUT_FILENO);

                    dup(m_sockfd);

                    execl(m_buf, m_buf, 0);
                    exit(0);
                }
            }
        }
    }

private:
    // Read buffer size.
    static const int BUFFER_SIZE = 1024;
    static int m_epollfd;

    int m_sockfd;

    sockaddr_in m_address;
    char m_buf[BUFFER_SIZE];

    // Marks the next position in the read buffer of the last byte of customer data that has been read.
    int m_read_idx;
};

int cgi_conn::m_epollfd = -1;

int main(int argc, char* argv[]) 
{
    if (argc <= 2) {

        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr*)& address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    processpool<cgi_conn>* pool = processpool<cgi_conn>::create(listenfd);

    if (pool) {

        pool->run();
        delete pool;
    }

    close(listenfd); /*As mentioned earlier, the main function creates the file descriptor listenfd, and then it closes it personally*/

    return 0;
}