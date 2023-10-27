#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

int a = 0;
int b = 0;

pthread_mutex_t mutex_a;
pthread_mutex_t mutex_b;

void* another(void* arg) {

    pthread_mutex_lock(&mutex_b);

    printf("in child thread, got mutex b, waiting for mutex a\n");
    sleep(5);

    ++b;

    pthread_mutex_lock(&mutex_a);

    b += a++;

    pthread_mutex_unlock(&mutex_a);
    pthread_mutex_unlock(&mutex_b);

    pthread_exit(nullptr);
}

int main()
{
    pthread_t id;

    pthread_mutex_init(&mutex_a, nullptr);
    pthread_mutex_init(&mutex_b, nullptr);

    pthread_create(&id, nullptr, another, nullptr);

    pthread_mutex_lock(&mutex_a);

    printf("in parent thread, got mutex a, waiting for mutex b\n");
    sleep(5);

    ++a;

    pthread_mutex_lock(&mutex_b);

    a += b++;

    pthread_mutex_unlock(&mutex_b);
    pthread_mutex_unlock(&mutex_a);

    pthread_join(id, nullptr);

    pthread_mutex_destroy(&mutex_a);
    pthread_mutex_destroy(&mutex_b);

    return 0;
}