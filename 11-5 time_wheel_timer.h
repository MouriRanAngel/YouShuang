#ifndef TIME_WHEEL_TIMER 
#define TIME_WHEEL_TIMER 

#include <time.h>
#include <netinet/in.h>
#include <stdio.h>

const int BUFFER_SIZE = 64; 

class tw_timer; 

// Bind socket and timer.
struct client_data { 
    
    sockaddr_in address; 
    int sockfd; 
    char buf[BUFFER_SIZE]; 
    tw_timer* timer; 
}; 

// Timer class.
class tw_timer { 
public: 
    tw_timer(int rot, int ts) : prev(nullptr), next(nullptr), rotation(rot), time_slot(ts) {}
public:
    int rotation;   // Record how many times the timer will take effect after the time rotation.
    int time_slot;  // Record which slot on the time wheel the timer belongs to (corresponding linked list, the same below).

    void(*cb_func)(client_data*);  // timer callback function.

    client_data* user_data;  // customer data.

    tw_timer* prev;  // Points to the previous timer.
    tw_timer* next;  // Points to the next timer.
};

class time_wheel {
public:
    time_wheel() : cur_slot(0) {

        for (int i = 0; i < N; ++i) {

            slots[i] = nullptr;  // Initialize the head node of each slot.
        }
    }

    ~time_wheel() {

        // Iterate through each slot and destroy the timer in it.
        for (int i = 0; i < N; ++i) {

            tw_timer* tmp = slots[i];

            while (tmp) {

                slots[i] = tmp->next;
                delete tmp;
                tmp = slots[i];
            }
        }
    }

    // Create a timer based on the timeout value 'timeout' and insert it into the appropriate slot.
    tw_timer* add_timer(int timeout) {

        if (timeout < 0) return nullptr;

        int ticks = 0;

        // Next, based on the timeout value of the timer to be inserted,
        // calculate how many ticks it will be triggered after the time wheel rotates,
        // and store the number of ticks in the variable ticks.
        // If the timeout value of the timer to be inserted is less than the slot interval SI of the time wheel,
        // the ticks are folded upward to 1, otherwise the ticks are folded downward to 'timeout / SI'.
        if (timeout < SI) {

            ticks = 1;
        }
        else {

            ticks = timeout / SI;
        }

        // Calculate how many turns the time wheel will 
        // take before the timer to be inserted is triggered.
        int rotation = ticks / N;

        // Calculate which slot the timer to 
        // be inserted should be inserted into.
        int ts = (cur_slot + (ticks % N)) % N;

        // Create a new timer, which is triggered after 
        // the time wheel rotates 'rotation' circles and is located on the 'ts'th slot.
        tw_timer* timer = new tw_timer(rotation, ts);

        // If there is no timer in the 'ts' slot, 
        // insert the new timer into it and set the timer to the head node of the slot.
        if (!slots[ts]) {

            printf("add timer, rotation is %d, ts is %d, cur_slot is %d\n", rotation, ts, cur_slot);

            slots[ts] = timer;
        }
        // Otherwise, insert the timer into the 'ts'th slot.
        else {

            timer->next = slots[ts];

            slots[ts]->prev = timer;
            slots[ts] = timer;
        }

        return timer;
    }

    // Delete target timer 'timer'.
    void del_timer(tw_timer* timer) {

        if (!timer) return;

        int ts = timer->time_slot;

        // slots[ts] is the head node of the slot where the target timer is located.
        // If the target timer is the head node, the head node of the ‘ts’ slot needs to be reset.
        if (timer == slots[ts]) {

            slots[ts] = slots[ts]->next;

            if (slots[ts]) {

                slots[ts]->prev = nullptr;
            }

            delete timer;
        }
        else {

            timer->prev->next = timer->next;

            if (timer->next) {

                timer->next->prev = timer->prev;
            }

            delete timer;
        }
    }

    // After the SI time is up, call this function and the time wheel will scroll forward by one slot interval.
    void tick() {

        tw_timer* tmp = slots[cur_slot];  // Get the head node of the current slot on the time wheel.

        printf("current slot is %d\n", cur_slot);

        while (tmp) {

            printf("tick the timer once\n");

            // If the timer's 'rotation' value is greater than 0,
            // it will have no effect in this round.
            if (tmp->rotation > 0) {

                --tmp->rotation;
                tmp = tmp->next;
            }
            // Otherwise, it means that the timer has expired, 
            // so execute the scheduled task and then delete the timer.
            else {

                tmp->cb_func(tmp->user_data);

                if (tmp == slots[cur_slot]) {

                    printf("delete header in cur_slot\n");

                    slots[cur_slot] = tmp->next;
                    delete tmp;

                    if (slots[cur_slot]) {

                        slots[cur_slot]->prev = nullptr;
                    }

                    tmp = slots[cur_slot];
                }
                else {

                    tmp->prev->next = tmp->next;

                    if (tmp->next) {

                        tmp->next->prev = tmp->prev;
                    }

                    tw_timer* tmp2 = tmp->next;
                    delete tmp;
                    
                    tmp = tmp2;
                    delete tmp2;
                }
            }
        }

        cur_slot = ++cur_slot % N;  // Updates the current slot of the Time Wheel to reflect the rotation of the Time Wheel.
    }

private:
    static const int N = 60;  // The number of slots on the time wheel.
    static const int SI = 1;  // The time wheel rotates once every 1 s, that is, the slot interval is 1 s.

    tw_timer* slots[N];  // The slot of the time wheel, where each element points to a timer linked list, and the linked list is unordered.
    int cur_slot;        // Current slot of the time wheel.
};

#endif