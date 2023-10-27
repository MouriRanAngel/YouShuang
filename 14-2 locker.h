#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// A class that encapsulates a semaphore.
class sem {
public:
    // create and initialize the semaphore.
    sem() {

        if (sem_init(&m_sem, 0, 0) != 0) {

            // The constructor has no return value and can report errors by throwing an exception.
            throw std::exception();
        }
    }

    // destroy semaphore.
    ~sem() {

        sem_destroy(&m_sem);
    }

    // wait for semaphore.
    bool wait() {

        return sem_wait(&m_sem) == 0;
    }

    // increase semaphore.
    bool post() {

        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

// A class that encapsulates a mutex lock.
class locker {
public:
    // create and initialize mutex lock.
    locker() {

        if (pthread_mutex_init(&m_mutex, nullptr) != 0) {

            throw std::exception();
        }
    }

    // destroy mutex lock.
    ~locker() {

        pthread_mutex_destroy(&m_mutex);
    }

    // get mutex lock.
    bool lock() {

        return pthread_mutex_lock(&m_mutex) == 0;
    }

    // release mutex lock.
    bool unlock() {

        return pthread_mutex_lock(&m_mutex) == 0;
    }

private:
    pthread_mutex_t m_mutex;
};

// Class that encapsulates condition variables.
class cond {
public:
    // create and initialize condition variables.
    cond() {

        if (pthread_mutex_init(&m_mutex, nullptr) != 0) {

            throw std::exception();
        }

        if (pthread_cond_init(&m_cond, nullptr) != 0) {

            // Once a problem occurs in the constructor,
            // you should immediately release the resources you have successfully allocated.
            pthread_mutex_destroy(&m_mutex);

            throw std::exception();
        }
    }

    // destroy condition variable.
    ~cond() {

        pthread_cond_destroy(&m_cond);
        pthread_mutex_destroy(&m_mutex);
    }

    // wait for condition variable.
    bool wait() {

        int ret = 0;

        pthread_mutex_lock(&m_mutex);

        ret = pthread_cond_wait(&m_cond, &m_mutex);

        pthread_mutex_unlock(&m_mutex);

        return ret == 0;
    }

    // wake up the thread waiting for the condition variable.
    bool signal() {

        return pthread_cond_signal(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
    pthread_mutex_t m_mutex;
};

#endif 