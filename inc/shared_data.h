#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include<stdio.h>
#include<pthread.h>

#define MAX_EVENTS          100
#define MAX_BUFFER_SIZE     1024
#define FIFO_FILE           "./logFIFO"
#define LOG_FILE            "./gateway.log"
#define SQL_database        "database.db"

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE);} while(0);

extern pthread_mutex_t log_lock;
extern pthread_mutex_t lock;
extern pthread_cond_t cond;

typedef struct
{
    int SensorNodeID;
    float temperature;
} Sensor_data;

typedef struct
{
    Sensor_data buffer[MAX_BUFFER_SIZE];
    int head;
    int tail;
    int handler_counter;
} Shared_data;

typedef struct
{
    int port;
    int len;
    Shared_data *shared_data;
}Thread_args;

#endif //SHARED_DATA_H