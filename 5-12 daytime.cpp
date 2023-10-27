#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

int main(int argc, char* argv[])
{
    assert(argc == 2);

    char* host = argv[1];

    // Get target host address information.
    struct hostent* hostinfo = gethostbyname(host);
    assert(hostinfo);

    // Get daytime service information.
    struct servent* servinfo = getservbyname("daytime", "tcp");
    assert(servinfo);

    printf("daytime port is %d.\n", ntohs(servinfo->s_port));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = servinfo->s_port;

    // Pay attention to the following code. Because h_addr_list itself is an address list using network byte order, 
    // there is no need to convert the byte order of the target IP address when using the IP address.
    address.sin_addr = *(struct in_addr*)* hostinfo->h_addr_list;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    int result = connect(sockfd, (struct sockaddr*)& address, sizeof(address));
    assert(result != -1);

    char buffer[128];

    result = read(sockfd, buffer, sizeof(buffer));
    assert(result > 0);

    buffer[result] = '\0';
    printf("the day tiem is: %s.", buffer);

    close(sockfd);
    return 0;
}