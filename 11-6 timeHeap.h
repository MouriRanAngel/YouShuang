#ifndef MIN_HEAP
#define MIN_HEAP

#include <iostream>
#include <netinet/in.h>
#include <time.h>

const int BUFFER_SIZE = 64;

class heap_timer;  // forward declaration.

// Bind socket and timer.
struct client_data {

    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    heap_timer* timer;
};

// Timer class.
class heap_timer {
public:
    heap_timer(int delay) {

        expire = time(nullptr) + delay;
    }

public:
    time_t expire;  // The absolute time when the timer takes effect.
    void (*cb_func)(client_data*);  // Timer callback function.
    client_data* user_data;  // User data.
};

// Time heap class.
class time_heap {
public:
    // Constructor 1, initializes an empty heap of size 'cap'.
    time_heap(int cap) : capacity(cap), cur_size(0) {

        try {

            array = new heap_timer*[capacity];  // Create a heap array.

            if (!array) throw std::exception();

            for (int i = 0; i < capacity; ++i) {

                array[i] = nullptr;
            }
        }
        catch (...) {

            throw;
        }
    }

    // Constructor 2, initialize the heap with an existing array.
    time_heap(heap_timer** init_array, int size, int capacity) : cur_size(size), capacity(capacity) {

        try {

            if (capacity < size) throw std::exception();

            array = new heap_timer*[capacity];  // Create a heap array.
            
            if (!array) throw std::exception();

            for (int i = 0; i < capacity; ++i) {

                array[i] = nullptr;
            }

            if (size != 0) {

                // initialize array.
                for (int i = 0; i < size; ++i) {

                    array[i] = init_array[i];
                }

                for (int i = (cur_size - 1) / 2; i >= 0; --i) {

                    // Perform a filter-down operation on the [(cur_size - 1) / 2] ~ 0th element in the array.
                    percolate_down(i);
                }
            }
        }
        catch (...) {

            throw;
        }
    }

    // destroy time heap.
    ~time_heap() {

        for (int i = 0; i < cur_size; ++i) {

            delete array[i];
        }

        delete[] array;
    }

public:
    // Add target timer 'timer'.
    void add_timer(heap_timer* timer) {

        try {

            if (!timer) return;

            if (cur_size >= capacity) {

                resize();  // If the current heap array capacity is not enough, double it.
            }

            // A new element is inserted, 
            // the current heap size is increased by one,
            // and 'hole' is the location of the new hole.
            int hole = cur_size++;
            
            int parent = 0;

            // Perform a filtering operation on all nodes on the path from the hole to the root node.
            for (; hole > 0; hole = parent) {

                parent = (hole - 1) / 2;

                if (array[parent]->expire <= timer->expire) break;

                array[hole] = array[parent];
            }

            array[hole] = timer;        
        }
        catch (...) {

            throw;
        }
    }

    // Delete target timer 'timer'.
    void del_timer(heap_timer* timer) {

        if (!timer) return;

        // Just set the callback function of the target timer to empty,
        // which is called delayed destruction.
        // This will save the overhead of actually deleting the timer,
        // but doing so tends to bloat the heap array.
        timer->cb_func = nullptr;
    }
    
    // Get the timer at the top of the heap.
    heap_timer* top() const {

        if (empty()) return nullptr;

        return array[0];
    }

    // Delete the timer at the top of the heap.
    void pop_timer() {

        if (empty()) return;

        if (array[0]) {

            delete array[0];

            // Replace the original top element of the heap with the last element in the heap array.
            array[0] = array[--cur_size];

            percolate_down(0);  // Perform a filter-down operation on the new top-of-heap element.
        }
    }

    // heartbeat function.
    void tick() {

        heap_timer* tmp = array[0];
        time_t cur = time(nullptr);

        // Loop through expired timers in the heap.
        while (!empty()) {

            if (!tmp) break;

            // If the timer on the top of the heap has not expired, exit the loop.
            if (tmp->expire > cur) break;

            // Otherwise, execute the task in the timer on the top of the heap.
            if (array[0]->cb_func) {

                array[0]->cb_func(array[0]->user_data);
            }

            // Delete the top element of the heap and generate a new timer on the top of the heap (array[0]).
            pop_timer();
            tmp = array[0];
        }
    }

    bool empty() const { return cur_size == 0; }

private:
    // Minimum heap reduction operation,
    // which ensures that the subtree with the 'hole'th node
    // as the root in the heap array has the minimum heap property.
    void percolate_down(int hole) {

        heap_timer* temp = array[hole];

        int child = 0;

        for (; ((hole * 2 + 1) <= (cur_size - 1)); hole = child) {

            child = hole * 2 + 1;

            if ((child < (cur_size - 1)) && (array[child + 1]->expire < array[child]->expire)) {

                ++child;
            }

            if (array[child]->expire < temp->expire) {

                array[hole] = array[child];
            }
            else {

                break;
            }
        }

        array[hole] = temp;
    }

    // Double the heap array capacity.
    void resize() {

        try {

            heap_timer** temp = new heap_timer*[2 * capacity];

            for (int i = 0; i < 2 * capacity; ++i) {

                temp[i] = nullptr;
            }

            if (!temp) throw std::exception();

            capacity = 2 * capacity;

            for (int i = 0; i < cur_size; ++i) {

                temp[i] = array[i];
                delete array[i];
            }

            delete[] array;
            array = temp;
        }
        catch (...) {

            throw;
        }
    }


private:
    heap_timer** array;  // heap array.
    int capacity;        // capacity of the heap array.
    int cur_size;        // number of elements currently contained in the heap array.
};

#endif