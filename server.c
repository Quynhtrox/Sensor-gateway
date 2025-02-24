#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>

#include "shared_data.h"
#include "handler_sensor.h"
#include "log.h"
#include "connection_manager.h"
#include "data_manager.h"
#include "storage_manager.h"

void log_process();

int main(int argc, char *argv[])
{
    if (argc < 2) 
    {
        printf("No port provided\n");
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    pid_t pid;
    Shared_data shared_data = {.head = 0, .tail = 0};
    Thread_args thread_ares = {.port = port, .shared_data = &shared_data};
    pthread_t threadconn, threaddata, threadstorage;

    pid = fork();
    if (pid == 0) 
    {
        log_process();

    } 
    else if (pid > 0) 
    { //Parent process
        printf("Im main process. My PID: %d\n", getpid());

        pthread_create(&threadconn, NULL, &thr_connection, &thread_ares);
        pthread_create(&threaddata, NULL, &thr_data, &shared_data);
        pthread_create(&threadstorage, NULL, &thr_storage, &shared_data);
        
        wait(NULL);
    }

    pthread_join(threadconn, NULL);
    pthread_join(threaddata, NULL);
    pthread_join(threadstorage, NULL);
    return 0;
}

/* Child process */
void log_process() {
    printf("Im log process. My PID: %d\n", getpid());

    mkfifo(FIFO_FILE, 0666);
    int fifo_fd, log_fd;
    char buff[MAX_BUFFER_SIZE];
    fifo_fd  = open(FIFO_FILE, O_RDONLY);
    log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);

    while(1) 
    {
        ssize_t bytes = read(fifo_fd, buff, sizeof(buff) - 1);
        if (bytes > 0) 
        {
            buff[bytes] = '\0';
            wr_log(&buff, log_fd);
        }
    }
    close(log_fd);
    close(fifo_fd);
    exit(0);
}
