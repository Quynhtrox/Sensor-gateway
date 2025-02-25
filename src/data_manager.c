#include<stdio.h>
#include<stdlib.h>
#include"data_manager.h"
#include"log.h"
#include"handler_sensor.h"
#include"wait_sensor_signal.h"

void send_temp_report(Sensor_data data);

/* Data manager thread */
void *thr_data(void *args) {
    Shared_data *shared = (Shared_data *)args;

    while (1)
    {
        Sensor_data data;
        data = wait_sensor_signal(shared);
        send_temp_report(data);
    }
}

/* Send the temperature reports to gateway.log */
void send_temp_report(Sensor_data data){
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