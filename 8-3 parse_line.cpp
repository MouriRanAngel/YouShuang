#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

const int BUFFER_SIZE = 4096;  // Read buffer size.

// The two possible states of the main state machine represent respectively: 
// the request line is currently being analyzed, and the header field is currently being analyzed.
enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER };

// The three possible states of the slave state machine, that is, the read state of the row, 
// respectively represent: a complete row is read, a row error occurs, and the row data is incomplete.
enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

// The result of the server processing the HTTP request: 
// NO_REQUEST indicates that the request is incomplete and needs to continue reading customer data; 
// GET_REQUEST indicates that a complete customer request was obtained; 
// BAD_REQUEST indicates that the customer request has a syntax error; 
// FORBIDDEN_REQUEST indicates that the customer does not have sufficient access rights to the resource; 
// INTERNAL_ERROR Indicates an internal error in the server; 
// CLOSED_CONNECTION indicates that the client has closed the connection.
enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };

// In order to simplify the problem, we do not send a complete HTTP response message to the client, 
// but only send the following success or failure information based on the server's processing results.
static const char* szret[] = { "I get a correct result\n", "Something wrong\n" };

// slave state machine, used to parse out a line of content.
LINE_STATUS prase_line(char* buffer, int& checked_index, int& read_index) {

    char temp;

    // 'checked_index' points to the byte currently being analyzed in the buffer (the application's read buffer),
    // 'read_index' points to the next byte at the end of the customer data in the buffer. 
    // The 0th to checked_index bytes in the buffer have been analyzed, 
    // and the checked_index to (read_index-1) bytes are analyzed one by one by the following loop.
    for (; checked_index < read_index; ++checked_index) {

        // Get the current bytes to be analyzed.
        temp = buffer[checked_index];

        // If the current byte is "\r", which is the carriage return character, 
        // it means that a complete line may be read.
        if (temp == '\r') {

            // If the "\r" character happens to be the last customer data that has been read in the current buffer, 
            // then this analysis does not read a complete line, 
            // and LINE_OPEN is returned to indicate that the customer data needs to continue to be read for further analysis.
            if ((checked_index + 1) == read_index) {

                return LINE_OPEN;
            }
            // If the next character is "\n", it means we successfully read a complete line.
            else if (buffer[checked_index + 1] == '\n') {

                buffer[checked_index++] = '\0';
                buffer[checked_index++] = '\0';

                return LINE_OK;
            }

            // Otherwise, it means that there is a syntax problem in the HTTP request sent by the client.
            return LINE_BAD;
        }
        // If the current byte is "\n", which is a newline character, it also indicates that a complete line may be read.
        else if (temp == '\n') {

            if ((checked_index > 1) && buffer[checked_index - 1] == '\r') {

                buffer[checked_index - 1] = '\0';
                buffer[checked_index++] = '\0';

                return LINE_OK;
            }

            return LINE_BAD;
        }
    }

    // If all content is analyzed and no "\r" character is encountered, LINE_OPEN is returned, 
    // indicating that customer data needs to be read for further analysis.
    return LINE_OPEN;
}

// Analyze request line.
HTTP_CODE parse_requestline(char* temp, CHECK_STATE& checkstate) {

    char* url = strpbrk(temp, "\t");

    // If there are no whitespace characters or "\t" characters in the request line, 
    // there must be something wrong with the HTTP request.
    if (!url) {

        return BAD_REQUEST;
    }

    *url++ = '\0';

    char* method = temp;

    if (strcasecmp(method, "GET") == 0) {  // Only supports GET method.

        printf("The request method is GET\n");
    }
    else {

        return BAD_REQUEST;
    }

    url += strspn(url, "\t");

    char* version = strpbrk(url, "\t");

    if (!version) {

        return BAD_REQUEST;
    }

    *version++ = '\0';
    version += strspn(version, "\t");

    // Only supports HTTP/1.1.
    if (strcasecmp(version, "HTTP/1.1") != 0) {

        return BAD_REQUEST;
    }

    // Check if the URL is legal.
    if (strncasecmp(url, "http://", 7) == 0) {

        url += 7;
        url = strchr(url, '/');
    }

    if (!url or url[0] != '/') {

        return BAD_REQUEST;
    }

    printf("The request URL is: %s\n", url);

    // After the HTTP request line is processed, the status is transferred to the analysis of the header field.
    checkstate = CHECK_STATE_HEADER;

    return NO_REQUEST;
}

// Analyze header fields.
HTTP_CODE parse_headers(char* temp) {

    // Encountering a blank line means we got a correct HTTP request.
    if (temp[0] == '\0') {

        return GET_REQUEST;
    }
    else if (strncasecmp(temp, "Host:", 5) == 0) {  // Processing the "HOST" header field.

        temp += 5;
        temp += strspn(temp, "\t");

        printf("the request host is: %s\n", temp);
    }
    else {  // Other header fields are not processed.

        printf("I can not handle this header\n");
    }

    return NO_REQUEST;
}

// Entry function for analyzing HTTP requests.
HTTP_CODE parse_content(char* buffer, int& checked_index, CHECK_STATE& checkstate, int& read_index, int& start_line) {

    LINE_STATUS linestatus = LINE_OK;  // Record the read status of the current row.
    HTTP_CODE retcode = NO_REQUEST;    // Record the processing results of HTTP requests.

    // Main state machine, used to fetch all complete lines from the buffer.
    while ((linestatus = prase_line(buffer, checked_index, read_index)) == LINE_OK) {

        char* temp = buffer + start_line;  // 'start_line' is the starting position of the line in the buffer.
        start_line = checked_index;        // Record the starting position of the next line.

        // 'checkstate' records the current state of the main state machine.
        switch (checkstate) {

            case CHECK_STATE_REQUESTLINE:  // The first state, analyzing the request line.
            
                {
                    retcode = parse_requestline(temp, checkstate);

                    if (retcode == BAD_REQUEST) {

                        return BAD_REQUEST;
                    }

                    break;
                }

            case CHECK_STATE_HEADER:  // The second state, analyzing the header field.

                {
                    retcode = parse_headers(temp);

                    if (retcode == BAD_REQUEST) {

                        return BAD_REQUEST;
                    }
                    else if (retcode == GET_REQUEST) {

                        return GET_REQUEST;
                    }

                    break;
                }
            
            default:

                return INTERNAL_ERROR;
        }
    }

    // If a complete row is not read, it means that the customer data needs to be read for further analysis.
    if (linestatus == LINE_OPEN) {

        return NO_REQUEST;
    }
    else {

        return BAD_REQUEST;
    }
}

int main(int argc, char* argv[]) 
{
    if (argc <= 2) {

        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = bind(listenfd, (struct sockaddr*)& address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    struct sockaddr_in client_address;
    socklen_t client_addresslength = sizeof(client_address);

    int fd = accept(listenfd, (struct sockaddr*)& client_address, &client_addresslength);

    if (fd < 0) {

        printf("errno is: %d\n", errno);
    }
    else {

        char buffer[BUFFER_SIZE];  // read buffer
        memset(buffer, '\0', BUFFER_SIZE);

        int data_read = 0; 
        int read_index = 0;     // How many bytes of customer data have been read so far.
        int checked_index = 0;  // How many bytes of customer data have been analyzed so far.
        int start_line = 0;     // The starting position of the line in the 'buffer'.

        // Set the initial state of the main state machine.
        CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;

        while (1) {  // Read customer data in a loop and analyze it.

            data_read = recv(fd, buffer + read_index, BUFFER_SIZE - read_index, 0);

            if (data_read == -1) {

                printf("reading failed\n");
                break;
            }
            else if (data_read == 0) {

                printf("remote client has closed the connection\n");
                break;
            }

            read_index += data_read;

            // Analyze all customer data obtained so far.
            HTTP_CODE result = parse_content(buffer, checked_index, checkstate, read_index, start_line);

            if (result == NO_REQUEST) {  // Haven't gotten a complete HTTP request yet.

                continue;
            }
            else if (result == GET_REQUEST) {  // Get a complete and correct HTTP request.

                send(fd, szret[0], strlen(szret[0]), 0);
                break;
            }
            else {  // Otherwise, an error occurs.

                send(fd, szret[1], strlen(szret[1]), 0);
                break;
            }
        }

        close(fd);
    }

    close(listenfd);
    return 0;
}