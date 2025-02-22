#include"handler_sensor.h"


/* Add data into buffer */
void add_data(Shared_data *shared, Sensor_data data) 
{
    shared->buffer[shared->tail] = data;
    shared->tail = (shared->tail + 1) % MAX_BUFFER_SIZE;
    shared->buffer[shared->tail] = data;
    shared->tail = (shared->tail + 1) % MAX_BUFFER_SIZE;
    shared->handler_counter = 2;
}

/* Get data from buffer */
Sensor_data get_data(Shared_data *shared) 
{
    Sensor_data data = {0};

    data = shared->buffer[shared->head];
    shared->head = (shared->head + 1) % MAX_BUFFER_SIZE;
    shared->handler_counter -= 1;

    return data;
}