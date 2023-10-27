#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

bool daemonize() {

    // Create a child process and close the parent process 
    // so that the program can run in the background.
    pid_t pid = fork();

    if (pid < 0) {

        return false;
    }
    else if (pid > 0) {

        exit(0);
    }

    // Set file permission mask. 
    // When a process creates a new file (using the 'open(const char* pathname, int flags, mode_t mode)' system call), 
    // the file's permissions will be mode &0777.
    umask(0);

    // Create a new session and set this process as the leader of the process group.
    pid_t sid = setsid();

    if (sid < 0) {

        return false;
    }

    // Switch working directory.
    if ((chdir("/")) < 0) {

        return false;
    }

    // Close the standard input, standard output, and standard error output devices.
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Close other open file descriptors, code omitted.

    // Direct standard input, standard output, and standard error output to the /dev/null file.
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);

    return true;
}