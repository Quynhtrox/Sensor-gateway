#include <stdio.h>
#include <pthread.h>
#include "wait_sensor_signal.h"
#include "handler_sensor.h"

Sensor_data wait_sensor_signal(Shared_data *shared) {
    pthread_mutex_lock(&lock);
    pthread_cond_wait(&cond, &lock);
    Sensor_data data;
    while (1)
    {
        if (shared->handler_counter) 
        {
            data = get_data(shared);
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return data;
}