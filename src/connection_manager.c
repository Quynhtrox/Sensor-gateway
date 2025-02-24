#include<stdio.h>
#include<stdlib.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<sys/wait.h>
#include<sys/types.h>
#include<pthread.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<errno.h>
#include<signal.h>
#include<string.h>
#include<fcntl.h>
#include<unistd.h>
#include"connection_manager.h"
#include"handler_sensor.h"
#include"log.h"

int make_socket_non_blocking(int socket_fd);
int create_server_socket(int port);
void sensor_connect(int new_socket_fd, int port);
void sensor_disconnect(int client_fd);
void handle_data_sensor(Shared_data *shared, char *buffer);

/* Connection manager thread */
void *thr_connection(void *args) {
    Thread_args *ThreadArgs = (Thread_args *)args;
    Shared_data *shared = ThreadArgs->shared_data;

    int event_count;
    int server_fd, new_socket_fd, epoll_fd;
    struct sockaddr_in sensoraddr;
    struct epoll_event event, events[MAX_EVENTS];
    char buffer[MAX_BUFFER_SIZE];

    //Make socket TCP
    server_fd = create_server_socket(ThreadArgs->port);

    //Attaching a value of '0' to the bytes of sensoraddr
    memset(&sensoraddr, 0, sizeof(struct sockaddr_in));

    //Get sensor's information
    ThreadArgs->len = sizeof(sensoraddr);

    // Make epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
        handle_error("epoll_create1()");

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
        for (int i = 0; i < event_count; i++) {
            if (events[i].data.fd == server_fd) {
                // Events on server socket (there's a new connection)
                while ((new_socket_fd = accept(server_fd, (struct sockaddr *)&sensoraddr, (socklen_t *)&ThreadArgs->len)) != -1) {
                    sensor_connect(new_socket_fd, ThreadArgs->port);
                    
                    // Add socket client's inf into epoll
                    event.events = EPOLLIN | EPOLLET; // Following read, edge-triggered mode
                    event.data.fd = new_socket_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket_fd, &event) == -1)
                        handle_error("epoll_ctl()");
                }
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    handle_error("accept()");
                    continue;
                }
            } 
            else {
                // Events on socket client
                int client_fd = events[i].data.fd;
                memset(buffer, 0, MAX_BUFFER_SIZE);
                int bytes_read = read(client_fd, buffer, MAX_BUFFER_SIZE - 1);
                if (bytes_read > 0) {
                    handle_data_sensor(shared, buffer);
                } 
                else {
                    sensor_disconnect(client_fd);
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

/* Creat server's socket*/
int create_server_socket(int port) {
    int opt = 0;
    int server_fd;
    struct sockaddr_in servaddr;

    //Attaching a value of '0' to the bytes of serveaddr
    memset(&servaddr, 0, sizeof(struct sockaddr_in));

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
    servaddr.sin_port = htons(port);
 
    //Bind socket to port
    if (bind(server_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
        handle_error("bind()");

    if (listen(server_fd, 5) == -1)
        handle_error("listen()");

    // Set server socket in non-blocking
    make_socket_non_blocking(server_fd);

    return server_fd;
}

/* Handle the sensor node connecting */
void sensor_connect(int new_socket_fd, int port) {
    printf("New connection accepted on port %d\n", port);

    // Write new connection into FIFO
    char log_message[MAX_BUFFER_SIZE];
    snprintf(log_message, sizeof(log_message),
            "There is a new sensor connection into port %d\n", port);
    log_events(log_message);

    // Set socket client to non-blocking mode
    make_socket_non_blocking(new_socket_fd);
}

/* Handle sensor's data */
void handle_data_sensor(Shared_data *shared, char *buffer) {
    printf("Received from client: %s\n", buffer);

    // Handling data from sensor
    Sensor_data data;
    sscanf(buffer, "%d %f", &data.SensorNodeID, &data.temperature);

    // Add data into buffer
    pthread_mutex_lock(&lock);
    add_data(shared, data);
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

/* Handle the sensor node disconnect */
void sensor_disconnect(int client_fd) {
    printf("Client disconnected\n");

    // Write there's a sensor disconnect into FIFO
    char log_message[MAX_BUFFER_SIZE];
    snprintf(log_message, sizeof(log_message),
            "The sensor has closed the connection\n");
    log_events(log_message);
    
    close(client_fd);
}