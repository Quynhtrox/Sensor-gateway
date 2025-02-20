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
#include<sys/epoll.h>

#define MAX_EVENTS          100
#define MAX_BUFFER_SIZE     1024
#define FIFO_FILE           "./logFIFO"
#define LOG_FILE            "./gateway.log"
#define SQL_database        "database.db"

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE);} while(0);

pthread_mutex_t log_lock =  PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock =      PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond =      PTHREAD_COND_INITIALIZER;

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

void *log_events(void *args);
void wr_log(void *args, int log_fd);
void add_data(Shared_data *shared, Sensor_data data);
Sensor_data get_data(Shared_data *shared);
int make_socket_non_blocking(int socket_fd);
static void *thr_connection(void *args);
static void *thr_data(void *args);
static void *thr_storage(void *args);
void log_process();

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("No port provided\n");
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    pid_t pid;
    Shared_data shared_data = {.head = 0, .tail = 0};
    Thread_args thread_ares = {.port = port, .shared_data = &shared_data};
    pthread_t threadconn, threaddata, threadstorage;

    pid = fork();
    if (pid == 0) {
        log_process();

    } else if (pid > 0) { //Parent process
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

/* Function write into FIFO */
void *log_events(void *args) {
    char *message = (char *)args;
    pthread_mutex_lock(&log_lock);
    int fd;
    fd = open(FIFO_FILE, O_RDWR | O_CREAT, 0666);

    if (fd == -1) {
        printf("call open() failed\n");
    } else {
        write(fd, message, strlen(message));
    }
    pthread_mutex_unlock(&log_lock);
    close(fd);
}

/* Function write into gateway.log */
void wr_log(void *args, int log_fd) {
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

/* Add data into buffer */
void add_data(Shared_data *shared, Sensor_data data) {

    shared->buffer[shared->tail] = data;
    shared->tail = (shared->tail + 1) % MAX_BUFFER_SIZE;
    shared->buffer[shared->tail] = data;
    shared->tail = (shared->tail + 1) % MAX_BUFFER_SIZE;
    shared->handler_counter = 2;

}

/* Get data from buffer */
Sensor_data get_data(Shared_data *shared) {
    Sensor_data data = {0};

    data = shared->buffer[shared->head];
    shared->head = (shared->head + 1) % MAX_BUFFER_SIZE;
    shared->handler_counter -= 1;
    return data;
}

/* Set the socket to non-blocking mode */
int make_socket_non_blocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1)
        handle_error("fcntl()");

    flags |= O_NONBLOCK;
    if (fcntl(socket_fd, F_SETFL, flags) == -1)
        handle_error("fcntl()");

    return 0;
}

/* Connection manager thread */
static void *thr_connection(void *args) {
    Thread_args *ThreadArgs = (Thread_args *)args;
    Shared_data *shared = ThreadArgs->shared_data;

    int opt = 0;
    int event_count;
    int server_fd, new_socket_fd, epoll_fd;
    struct sockaddr_in servaddr, sensoraddr;
    struct epoll_event event, events[MAX_EVENTS];
    char buffer[MAX_BUFFER_SIZE];
    
    //Attaching a value of '0' to the bytes of serveaddr, sensoradd
    memset(&servaddr, 0, sizeof(struct sockaddr_in));
    memset(&sensoraddr, 0, sizeof(struct sockaddr_in));

    //Make socket TCP
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
        handle_error("socket()");

    //Prevent issues: "address already in use"
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1)
        handle_error("setsockopt()");

    //Initialize the address for the port
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(ThreadArgs->port);

    //Bind socket to port
    if (bind(server_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
        handle_error("bind()");

    if (listen(server_fd, 5) == -1)
        handle_error("listen()");

    //Get sensor's information
    ThreadArgs->len = sizeof(sensoraddr);

    // Make epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
        handle_error("epoll_create1()");

    // Set server socket in non-blocking
    make_socket_non_blocking(server_fd);

    // Add server socket into epoll 
    event.events = EPOLLIN; // Following read
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1)
        handle_error("epoll_ctl()");

    printf("Connection Manager listening on port %d...\n", ThreadArgs->port);

    /* Main event loop */
    while (1) {
        // Wait event
        event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (event_count == -1)
            handle_error("epoll_wait()");

        // Browse event that occur
        for (int i = 0; i < event_count; i++){
            if (events[i].data.fd == server_fd) {
                // Events on server socket (there's a new connection)
                while ((new_socket_fd = accept(server_fd, (struct sockaddr *)&sensoraddr, (socklen_t *)&ThreadArgs->len)) != -1) {
                    printf("New connection accepted on port %d\n", ThreadArgs->port); 

                    // Write new connection into FIFO
                    char log_message[MAX_BUFFER_SIZE];
                    snprintf(log_message, sizeof(log_message),
                            "There is a new sensor connection into port %d\n", ThreadArgs->port);
                    log_events(log_message);

                    // Set socket client to non-blocking mode
                    make_socket_non_blocking(new_socket_fd);

                    // Add socket client's inf into epoll
                    event.events = EPOLLIN | EPOLLET; // Following read, edge-triggered mode
                    event.data.fd = new_socket_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket_fd, &event) == -1) 
                        handle_error("epoll_ctl()");
                }

                if (errno != EAGAIN && errno != EWOULDBLOCK){
                    handle_error("accept()");
                    continue;
                }
            
            } else {
                // Events on socket client
                int client_fd = events[i].data.fd;

                memset(buffer, 0, MAX_BUFFER_SIZE);
                int bytes_read = read(client_fd, buffer, MAX_BUFFER_SIZE - 1);

                if (bytes_read > 0) {
                    printf("Received from client: %s\n", buffer);

                    /* Handling data from sensor */
                    Sensor_data data;
                    sscanf(buffer, "%d %f", &data.SensorNodeID, &data.temperature);

                    /* Add data into buffer */
                    pthread_mutex_lock(&lock);
                    add_data(shared, data);
                    pthread_cond_broadcast(&cond);
                    pthread_mutex_unlock(&lock);

                } else {
                    printf("Client disconnected\n");
                    
                    Sensor_data data = get_data(shared);

                    /* Write there's a sensor disconnect into FIFO */
                    char log_message[MAX_BUFFER_SIZE];
                    snprintf(log_message, sizeof(log_message),
                            "The sensor has closed the connection\n");
                    log_events(log_message);
                    close(client_fd);
                    break;
                }
            }
        }
    }
    // close(new_socket_fd);
    close(server_fd);
    close(epoll_fd);
    return NULL;
}

/* Data manager thread */
static void *thr_data(void *args) {
    printf("i'm thread data\n");
    Shared_data *shared = (Shared_data *)args;

    while (1) 
    {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&cond, &lock);
        Sensor_data data;
        while (1)
        {
            if (shared->handler_counter) {
                data = get_data(shared);
                break;
            }
        }
        pthread_mutex_unlock(&lock);

        if (data.SensorNodeID !=0 ) {
            char log_message[MAX_BUFFER_SIZE];
            if (data.temperature > 30.0) {
                snprintf(log_message, sizeof(log_message),
                        "The sensor node with %d reports it’s too hot (running avg temperature = %.1f)\n",
                        data.SensorNodeID, data.temperature);
                log_events(log_message);
            } else if (data.temperature < 15) {
                snprintf(log_message, sizeof(log_message),
                        "The sensor node with %d reports it’s too cold (running avg temperature = %.1f)\n",
                        data.SensorNodeID, data.temperature);
                log_events(log_message);
            }
        }
    }
}

//Storage manager thread
static void *thr_storage(void *args) {
    printf("i'm thread storage\n");
    Shared_data *shared = (Shared_data *)args;
    sqlite3 *db;        // Databases pointer
    char *errMsg = 0;   // Error issue message
    int rc;             // Return issue from SQLite
    const char *sql;    // Statement SQL
    char log_message[MAX_BUFFER_SIZE];
    char sql_query[MAX_BUFFER_SIZE];

    rc = sqlite3_open(SQL_database, &db); // Open or create database
    if (rc != SQLITE_OK) {
        snprintf(log_message, sizeof(log_message),
                "Connection to SQL server lost.\n");
        log_events(log_message);
    } else if (rc == SQLITE_OK) {
        snprintf(log_message, sizeof(log_message),
                "Connection to SQL server established.\n");
        log_events(log_message);
    }
    // SQL statement execution
    sql =   "CREATE TABLE IF NOT EXISTS Sensor_data (" \
            "SENSOR_ID INTERGER PRIMARY KEY," \
            "Temperature FLOAT );";
    // Create data table
    rc = sqlite3_exec(db, sql, NULL, 0, &errMsg);
    if (rc != SQLITE_OK) {
        snprintf(log_message, sizeof(log_message),
                "Created new table %s failed.\n", SQL_database);
        log_events(log_message);
    } else {
        snprintf(log_message, sizeof(log_message),
                "New table %s created.\n",SQL_database);
        log_events(log_message);
    }

    while (1) {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&cond, &lock);
        Sensor_data data;
        while (1)
        {
            if (shared->handler_counter) {
                data = get_data(shared);
                break;
            }
        }
        pthread_mutex_unlock(&lock);

        if (data.SensorNodeID != 0) {
            //insert or replace sensor's data into SQL database
            snprintf(sql_query, sizeof(sql_query),
                    "INSERT OR REPLACE INTO Sensor_data (SENSOR_ID, Temperature) VALUES (%d, %.2f);",
                    data.SensorNodeID, data.temperature);
            rc = sqlite3_exec(db, sql_query, NULL, 0, &errMsg);
            if (rc != SQLITE_OK) {
                snprintf(log_message, sizeof(log_message), "Data insertion failed\n");
                log_events(log_message);
            } else {
                snprintf(log_message, sizeof(log_message), "Successful data insertion\n");
                log_events(log_message);
            }
        }
        
    }
    sqlite3_close(db);
}

//Child process
void log_process() {
    printf("Im log process. My PID: %d\n", getpid());

    mkfifo(FIFO_FILE, 0666);
    int fifo_fd, log_fd;
    char buff[MAX_BUFFER_SIZE];
    fifo_fd  = open(FIFO_FILE, O_RDONLY);
    log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);

    while(1) {
        ssize_t bytes = read(fifo_fd, buff, sizeof(buff) - 1);
        if (bytes > 0) {
            buff[bytes] = '\0';
            wr_log(&buff, log_fd);
        }
    }
    close(log_fd);
    close(fifo_fd);
    exit(0);
}