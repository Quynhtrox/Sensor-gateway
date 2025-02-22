#include<stdio.h>
#include<stdlib.h>
#include"data_manager.h"
#include"log.h"
#include"handler_sensor.h"

/* Data manager thread */
void *thr_data(void *args) {
    printf("i'm thread data\n");
    Shared_data *shared = (Shared_data *)args;

    while (1) 
    {
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

        if (data.SensorNodeID !=0 ) 
        {
            char log_message[MAX_BUFFER_SIZE];
            if (data.temperature > 30.0) 
            {
                snprintf(log_message, sizeof(log_message),
                        "The sensor node with %d reports it’s too hot (running avg temperature = %.1f)\n",
                        data.SensorNodeID, data.temperature);
                log_events(log_message);
            } 
            else if (data.temperature < 15) 
            {
                snprintf(log_message, sizeof(log_message),
                        "The sensor node with %d reports it’s too cold (running avg temperature = %.1f)\n",
                        data.SensorNodeID, data.temperature);
                log_events(log_message);
            }
        }
    }
}