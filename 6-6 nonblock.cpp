// Set file descriptor to non-blocking.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int setnonblocking(int fd) {

    // Get the old status flag of the file descriptor.
    int old_option = fcntl(fd, F_GETFL);

    // Set non-blocking flag.
    int new_option = old_option | O_NONBLOCK;

    fcntl(fd, F_SETFL, new_option);

    // Returns the old status flag of the file descriptor so that it can be restored later.
    return old_option;
}