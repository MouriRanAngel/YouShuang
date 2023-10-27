#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "14-2 locker.h"

class http_conn {
public:
    // Maximum length of file name.
    static const int FILENAME_LEN = 200;

    // Read buffer size.
    static const int READ_BUFFER_SIZE = 2048;

    // Write buffer size.
    static const int WRITE_BUFFER_SIZE = 1024;

    // HTTP request method, but we only support GET.
    enum METHOD {

        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATCH
    };

    // The state of the main state machine when parsing a client request (recall Chapter 8).
    enum CHECK_STATE {

        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    // Possible results of server handling of HTTP requests.
    enum HTTP_CODE {

        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    // Row read status.
    enum LINE_STATUS {

        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // Initialize newly accepted connections.
    void init(int sockfd, const sockaddr_in& addr);

    // close connection.
    void close_conn(bool real_close = true);

    // Handle customer requests.
    void process();

    // non-blocking read operation.
    bool read();

    // non-blocking write operation.
    bool write();

private:
    // Initialize connection.
    void init();

    // Parse HTTP requests.
    HTTP_CODE process_read();

    // Populate HTTP response.
    bool process_write(HTTP_CODE ret);

    // The following set of functions are called by process_read to analyze HTTP requests.
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();

    char* get_line() {

        return m_read_buf + m_start_line;
    }

    LINE_STATUS parse_line();

    // The following set of functions are called by process_write to populate the HTTP response.
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    // All events on the socket are registered in the same epoll kernel event table,
    // so the epoll file descriptor is set to static.
    static int m_epollfd;

    // Count the number of users.
    static int m_user_count;

private:
    // The socket of the HTTP connection and the other party’s socket address.
    int m_sockfd;
    sockaddr_in m_address;

    // read buffer.
    char m_read_buf[READ_BUFFER_SIZE];

    // Identifies the next position in the read buffer of the last byte of customer data that has been read.
    int m_read_idx;

    // The position of the character currently being analyzed in the read buffer.
    int m_checked_idx;

    // The starting position of the line currently being parsed.
    int m_start_line;

    // write buffer.
    char m_write_buf[WRITE_BUFFER_SIZE];

    // Number of bytes in the write buffer to be sent.
    int m_write_idx;

    // The current state of the main state machine.
    CHECK_STATE m_check_state;

    // Request method.
    METHOD m_method;

    // The full path of the target file requested by the customer,
    // its content is equal to 'doc_root + m_url', 'doc_root' is the website root directory.
    char m_real_file[FILENAME_LEN];

    // The file name of the target file requested by the client.
    char* m_url;

    // HTTP protocol version number, we only support HTTP/1.1.
    char* m_version;

    // Hostname.
    char* m_host;

    // The length of the HTTP request message body.
    int m_content_length;

    // Does the HTTP request require the connection to be kept alive?
    bool m_linger;

    // The target file requested by the client is mmapped to the starting location in memory.
    char* m_file_address;

    // The status of the target file. Through it, we can determine whether the file exists,
    // whether it is a directory, whether it is readable, and obtain information such as file size.
    struct stat m_file_stat;

    // We will use writev to perform write operations, so define the following two members,
    // where 'm_iv_count' represents the number of memory blocks written.
    struct iovec m_iv[2];
    int m_iv_count;
};

#endif