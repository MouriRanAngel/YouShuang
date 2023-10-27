#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>

const int BUFFER_SIZE = 64;
class util_timer;  // forward declaration.

// User data structure: client socket address, socket file descriptor, read cache and timer.
struct client_data {

    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    util_timer* timer;
};

// Timer class.
class util_timer {
public:
    util_timer() : prev(nullptr), next(nullptr) {}

public:
    time_t expire;                  // The timeout of the task, absolute time is used here.
    void (*cb_func)(client_data*);  // Task callback function.

    // The customer data processed by the callback function is passed
    // to the callback function by the executor of the timer.
    client_data* user_data;         

    util_timer* prev;  // Points to the previous timer.
    util_timer* next;  // Points to the next timer.
};

// Timer linked list. It is an ascending, doubly linked list with a head node and a tail node.
class sort_timer_lst {
public:
    sort_timer_lst() : head(nullptr), tail(nullptr) {}

    // When the linked list is destroyed, all timers in it are deleted.
    ~sort_timer_lst() {

        util_timer* tmp = head;

        while (tmp) {

            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    // Add the target timer to the linked list.
    void add_timer(util_timer* timer) {

        if (!timer) return;

        if (!head) {

            head = tail = timer;
            return;
        }

        // If the timeout time of the target timer is less than the timeout time of 
        // all timers in the current linked list, the timer is inserted into the head 
        // of the linked list as the new head node of the linked list. 
        // Otherwise, you need to call the overloaded function 
        // 'add_timer (util_timer* timer, util_timer* lst_head)'
        // and insert it into the appropriate position in the linked list to 
        // ensure the ascending order characteristic of the linked list.
        if (timer->expire < head->expire) {

            timer->next = head;
            head->prev = timer;
            head = timer;

            return;
        }

        add_timer(timer, head);
    }

    // When a scheduled task changes, adjust the position of the corresponding timer in the linked list. 
    // This function only considers the extended timeout of the adjusted timer, 
    // that is, the timer needs to be moved to the end of the linked list.
    void adjust_timer(util_timer* timer) {

        if (!timer) return;

        util_timer* tmp = timer->next;

        // If the adjusted target timer is at the end of the linked list, 
        // or the new timeout value of the timer is still less than the timeout value of the next timer, 
        // no adjustment is required.
        if (!tmp or (timer->expire < tmp->expire)) return;

        // If the target timer is the head node of the linked list, 
        // remove the timer from the linked list and reinsert it into the linked list.
        if (timer == head) {

            head = head->next;
            head->prev = nullptr;
            timer->next = nullptr;

            add_timer(timer, head);
        }
        // If the target timer is not the head node of the linked list, 
        // the timer is taken out of the linked list and 
        // then inserted into the part of the linked list after its original location.
        else {

            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;

            add_timer(timer, timer->next);
        }
    }

    // Delete the target timer 'timer' from the linked list.
    void del_timer(util_timer* timer) {

        if (!timer) return;

        // The following condition is true, 
        // which means that there is only one timer in the linked list,
        // which is the target timer.
        if ((timer == head) && (timer == tail)) {

            head = tail = nullptr;

            delete timer;
            return;
        }
        
        // If there are at least two timers in the linked list, 
        // and the target timer is the head node of the linked list, 
        // reset the head node of the linked list to the next node of the original head node, 
        // and then delete the target timer.
        if (timer == head) {

            head = head->next;
            head->prev = nullptr;

            delete timer;
            return;
        }

        // If there are at least two timers in the linked list, 
        // and the target timer is the tail node of the linked list, 
        // reset the tail node of the linked list to the node before the original tail node, 
        // and then delete the target timer.
        if (timer == tail) {

            tail = tail->prev;
            tail->next = nullptr;

            delete timer;
            return;
        }

        // If the target timer is in the middle of the linked list, 
        // concatenate the timers before and after it, and then delete the target timer.
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;

        delete timer;
    }

    // Each time the SIGALRM signal is triggered, 
    // a tick function is executed in its signal processing function 
    // (if the unified event source is used, the main function) to process the tasks that expire on the linked list.
    void tick() {

        if (!head) return;

        printf("timer tick\n");

        time_t cur = time(nullptr);  // Get the current system time.
        util_timer* tmp = head;

        // Process each timer in sequence starting from the head node 
        // until encountering a timer that has not yet expired. 
        // This is the core logic of the timer.
        while (tmp) {

            // Because each timer uses absolute time as the timeout value,
            // we can compare the timer's timeout value with the current system time 
            // to determine whether the timer has expired.
            if (cur < tmp->expire) break;

            // Call the timer's callback function to perform scheduled tasks.
            tmp->cb_func(tmp->user_data);

            // After executing the scheduled task in the timer, 
            // delete it from the linked list and reset the head node of the linked list.
            head = tmp->next;

            if (head) {

                head->prev = nullptr;
            }

            delete tmp;
            tmp = head;
        }
    }

private:
    // An overloaded helper function that is called by the public 'add_timer' function and 'adjust_timer' function.
    // This function means adding the target timer 'timer' to the partial linked list after the node 'lst_head'.
    void add_timer(util_timer* timer, util_timer* lst_head) {

        util_timer* prev = lst_head;
        util_timer* tmp = prev->next;

        // Traverse the part of the linked list after the 'lst_head' node until a node with a timeout greater than 
        // the timeout of the target timer is found, and insert the target timer before the node.
        while (tmp) {

            if (timer->expire < tmp->expire) {

                prev->next = timer;
                timer->next = tmp;

                tmp->prev = timer;
                timer->prev = prev;

                break;
            }

            prev = tmp;
            tmp = tmp->next;
        }

        // If the part of the linked list after the 'lst_head' node is traversed and 
        // a node with a timeout greater than the timeout of the target timer is not found, 
        // insert the target timer into the end of the linked list and 
        // set it as the new tail node of the linked list.
        if (!tmp) {

            prev->next = timer;
            timer->prev = prev;
            timer->next = nullptr;
            tail = timer;
        }
    }

private:
    util_timer* head;
    util_timer* tail;
};

#endif