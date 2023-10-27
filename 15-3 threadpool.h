#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

// Reference to the wrapper class of the thread synchronization mechanism introduced in Chapter 14.
#include "14-2 locker.h"

// Thread pool class, defined as a template class for code reuse. Template parameter T is the task class.
template<typename T>
class threadpool {
public:
    // The parameter thread_number is the number of threads in the thread pool,
    // and max_requests is the maximum number of requests waiting to be processed in the request queue.
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();

    // Add tasks to the request queue.
    bool append(T* request);

private:
    // A function run by a worker thread, which continuously removes tasks from the work queue and executes them.
    static void* worker(void* arg);

    void run();

private:
    int m_thread_number;  // Number of threads in the thread pool.
    int m_max_requests;   // Maximum number of requests allowed in the request queue.

    bool m_stop;                // Whether to end the thread.
    sem m_queuestat;            // Are there any tasks that need to be processed?
    locker m_queuelocker;       // Mutex protecting request queue.
    pthread_t* m_threads;       // An array describing the thread pool with size m_thread_number.
    std::list<T*> m_workqueue;  // request queue.
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : m_thread_number(thread_number),
    m_max_requests(max_requests), m_stop(false), m_threads(nullptr) {

    if ((thread_number <= 0) or (max_requests <= 0)) {

        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];

    if (!m_threads) {

        throw std::exception();
    }

    // Create thread_number threads and set them all to detach threads.
    for (int i = 0; i < thread_number; ++i) {

        printf("create the %d-th thread\n", i);

        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {

            delete[] m_threads;
            throw std::exception();
        }

        if (pthread_detach(m_threads[i])) {

            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool() {

    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request) {

    // Be sure to lock when operating the work queue because it is shared by all threads.
    m_queuelocker.lock();

    if (m_workqueue.size() > m_max_requests) {

        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();

    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg) {

    threadpool* pool = (threadpool*) arg;
    pool->run();

    return pool;
}

template<typename T>
void threadpool<T>::run() {

    while (!m_stop) {

        m_queuestat.wait();
        m_queuelocker.lock();

        if (m_workqueue.empty()) {

            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue.front();

        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request) continue;

        request->process();
    }
}

#endif