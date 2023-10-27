#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <wait.h>

pthread_mutex_t mutex;

// Functions run by child threads.
// It first acquires the mutex lock,
// then pauses for 5 s, and then releases the mutex lock.
void* another(void* arg) {

    printf("in child thread, lock the mutex\n");

    pthread_mutex_lock(&mutex);

    sleep(5);

    pthread_mutex_unlock(&mutex);
}

void prepare() {

    pthread_mutex_lock(&mutex);
}

void infork() {

    pthread_mutex_unlock(&mutex);
}

int main()
{
    pthread_mutex_init(&mutex, nullptr);
    pthread_t id;

    // pthread_create(&id, nullptr, another, nullptr);
    pthread_atfork(prepare, infork, infork);

    // The main thread in the parent process pauses for 1 s to ensure that
    // the child thread has started running and 
    // obtained the mutex variable 'mutex' before performing the fork operation.
    sleep(1);

    int pid = fork();

    if (pid < 0) {

        pthread_join(id, nullptr);
        pthread_mutex_destroy(&mutex);

        return 1;
    }
    else if (pid == 0) {

        printf("I am in the child, want to get the lock\n");

        // The child process inherits the status of the mutex lock mutex from the parent process.
        // The mutex lock is in a locked state.
        // This is caused by the child thread in the parent process executing pthread_mutex_lock. 
        // Therefore, the following locking operation will always be blocked.
        // Although logically it shouldn't block.
        pthread_mutex_lock(&mutex);

        printf("I can not run to here, oop...\n");

        pthread_mutex_unlock(&mutex);

        exit(0);
    }
    else {

        wait(nullptr);
    }

    pthread_join(id, nullptr);
    pthread_mutex_destroy(&mutex);

    return 0;
}