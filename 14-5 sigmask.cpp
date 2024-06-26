#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#define handle_error_en(en, msg)  do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

static void* sig_thread(void* arg) {

    sigset_t* set = (sigset_t*) arg;

    int s, sig;

    for (;;) {

        // The second step is to call sigwait to wait for the signal.
        s = sigwait(set, &sig);

        if (s != 0) {

            handle_error_en(s, "sigwait");
        }
            
        printf("Signal handling thread got signal %d\n", sig);
    }
}

int main(int argc, char *argv[])
{
    pthread_t thread;
    sigset_t set;
    int s;

    // The first step is to set the signal mask in the main thread.
    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGUSR1);

    s = pthread_sigmask(SIG_BLOCK, &set, nullptr);

    if (s != 0) {

        handle_error_en(s, "pthread_sigmask");
    }

    s = pthread_create(&thread, nullptr, &sig_thread, (void*)& set);
        
    if (s != 0) {

        handle_error_en(s, "pthread_create");
    }

    pause();      
}