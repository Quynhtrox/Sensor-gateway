#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include "log.h"

/* Function write into FIFO */
void *log_events(void *args) 
{
    char *message = (char *)args;
    pthread_mutex_lock(&log_lock);
    int fd;
    fd = open(FIFO_FILE, O_RDWR | O_CREAT, 0666);

    if (fd == -1) 
    {
        printf("call open() failed\n");
    } 
    else 
    {
        write(fd, message, strlen(message));
    }
    pthread_mutex_unlock(&log_lock);
    close(fd);
}

/* Function write into gateway.log */
void wr_log(void *args, int log_fd) 
{
    static int a = 0;
    a++; // Sequence_number
    char *message = (char *)args;
    char buffer[MAX_BUFFER_SIZE];

    time_t current_time = time(NULL);
    struct tm *time_info = localtime(&current_time);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);

    snprintf(buffer,sizeof(buffer),"%d. (%s)    %s.\n", a, timestamp, message);
    write(log_fd, buffer, strlen(buffer));
}