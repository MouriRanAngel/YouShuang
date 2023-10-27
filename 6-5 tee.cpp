#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

int main(int argc, char* argv[])
{
    if (argc != 2) {

        printf("usage: %s<file>\n", argv[0]);
        return 1;
    }

    int filefd = open(argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0666);
    assert(filefd > 0);

    int pipefd_stdout[2];
    int ret = pipe(pipefd_stdout);
    assert(ret != -1);

    int pipefd_file[2];
    ret = pipe(pipefd_file);
    assert(ret != -1);

    // Pass the contents of standard input into pipefd_stdout.
    ret = splice(STDIN_FILENO, nullptr, pipefd_stdout[1], nullptr, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    assert(ret != -1);

    // Copies the output of pipefd_stdout to the input of pipefd_file.
    ret = tee(pipefd_stdout[0], pipefd_file[1], 32768, SPLICE_F_NONBLOCK);
    assert(ret != -1);

    // Direct the output of the pipe pipefd_file to the file descriptor filefd, 
    // thereby writing the contents of the standard input to the file.
    ret = splice(pipefd_file[0], nullptr, filefd, nullptr, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    assert(ret != -1);

    // Direct the output of the pipefd_stdout to the standard output, 
    // and its content is exactly the same as the content written to the file.
    ret = splice(pipefd_stdout[0], nullptr, STDOUT_FILENO, nullptr, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    assert(ret != -1);

    close(filefd);
    close(pipefd_stdout[0]);
    close(pipefd_stdout[1]);
    close(pipefd_file[0]);
    close(pipefd_file[1]);

    return 0;
}