#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>     //  Chứa cấu trúc cần thiết cho socket. 
#include <netinet/in.h>     //  Thư viện chứa các hằng số, cấu trúc khi sử dụng địa chỉ trên internet
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
} Shared_data;

typedef struct
{
    int port;
    int len;
    Shared_data *shared_data;
}Thread_args;

//Hàm ghi vào FIFO
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

//Hàm ghi vào gateway.log
void wr_log(void *args, int log_fd) {
    static int a = 0;
    a++; //a là sequence_number
    char *message = (char *)args;
    char buffer[MAX_BUFFER_SIZE];

    time_t current_time = time(NULL);
    struct tm *time_info = localtime(&current_time);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);

    snprintf(buffer,sizeof(buffer),"%d. (%s)    %s.\n", a, timestamp, message);
    write(log_fd, buffer, strlen(buffer));
}

//Add data into buffer
void add_data(Shared_data *shared, Sensor_data data) {

    shared->buffer[shared->tail] = data;
    shared->tail = (shared->tail + 1) % MAX_BUFFER_SIZE;

}

//Get data from buffer
Sensor_data get_data(Shared_data *shared) {
    Sensor_data data = {0};

    data = shared->buffer[shared->head];
    shared->head = (shared->head + 1) % MAX_BUFFER_SIZE;

    return data;
}

/* Hàm đặt socket ở chế đọ non-blocking */
int make_socket_non_blocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1)
        handle_error("fcntl()");

    flags |= O_NONBLOCK;
    if (fcntl(socket_fd, F_SETFL, flags) == -1)
        handle_error("fcntl()");

    return 0;
}

//Connection manager thread
static void *thr_connection(void *args) {
    Thread_args *ThreadArgs = (Thread_args *)args;
    Shared_data *shared = ThreadArgs->shared_data;

    int opt = 0;
    int event_count;
    int server_fd, new_socket_fd, epoll_fd;
    struct sockaddr_in servaddr, sensoraddr;
    struct epoll_event event, events[MAX_EVENTS];
    char buffer[MAX_BUFFER_SIZE];
    
    //Gắn giá trị 0 vào các byte của servaddr, sensoraddr
    memset(&servaddr, 0, sizeof(struct sockaddr_in));
    memset(&sensoraddr, 0, sizeof(struct sockaddr_in));

    //Tạo socket TCP
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
        handle_error("socket()");

    //Ngăn lỗi: "address already in use"
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1)
        handle_error("setsockopt()");

    //Khởi tạo địa chỉ cho cổng
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(ThreadArgs->port);

    //Gắn socket vào cổng
    if (bind(server_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
        handle_error("bind()");

    if (listen(server_fd, 5) == -1)
        handle_error("listen()");

    // Lấy thông tin sensor
    ThreadArgs->len = sizeof(sensoraddr);

    /* Make epoll instance */
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
        handle_error("epoll_create1()");

    /* Set server socket in non-blocking */
    make_socket_non_blocking(server_fd);

    /* Add server socket into epoll */
    event.events = EPOLLIN; // Following read
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1)
        handle_error("epoll_ctl()");

    printf("Connection Manager listening on port %d...\n", ThreadArgs->port);

    /* Vòng lặp sự kiện chính */
    while (1) {
        // Chờ sự kiện
        event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (event_count == -1)
            handle_error("epoll_wait()");

        // Duyệt qua các sự kiện xảy ra
        for (int i = 0; i < event_count; i++){
            if (events[i].data.fd == server_fd) {
                // Sự kiện trên server socket (kết nối mới)
                while ((new_socket_fd = accept(server_fd, (struct sockaddr *)&sensoraddr, (socklen_t *)&ThreadArgs->len)) != -1) {
                    printf("New connection accepted on port %d\n", ThreadArgs->port); 

                    // Đặt socket client ở chế độ non-blocking
                    make_socket_non_blocking(new_socket_fd);

                    // Thêm socket client vào epoll
                    event.events = EPOLLIN | EPOLLET; // Theo dõi đọc, chế độ edge-triggered
                    event.data.fd = new_socket_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket_fd, &event) == -1) 
                        handle_error("epoll_ctl()");
                }

                if (errno != EAGAIN && errno != EWOULDBLOCK){
                    handle_error("accept()");
                    continue;
                }
            
            } else {
                //Sự kiện trên socket client
                int client_fd = events[i].data.fd;

                memset(buffer, 0, MAX_BUFFER_SIZE);
                int bytes_read = read(client_fd, buffer, MAX_BUFFER_SIZE - 1);

                if (bytes_read > 0) {
                    printf("Received from client: %s\n", buffer);

                    // Giải mã dữ liệu từ sensor
                    Sensor_data data;
                    sscanf(buffer, "%d %f", &data.SensorNodeID, &data.temperature);

                    //Thêm dữ liệu vào shared buffer
                    pthread_mutex_lock(&lock);
                    add_data(shared, data);
                    pthread_cond_signal(&cond);
                    pthread_mutex_unlock(&lock);

                } else {
                    printf("Client disconnected\n");
                    
                    Sensor_data data = get_data(shared);

                    //Ghi log closed
                    char log_message[MAX_BUFFER_SIZE];
                    snprintf(log_message, sizeof(log_message), "The sensor node with %d has closed the connection\n", data.SensorNodeID);
                    log_events(log_message);
                    close(client_fd);
                    break;
                }
            }
        }


        //{ new_socket_fd = accept(server_fd, (struct sockaddr *)&sensoraddr, (socklen_t *)&ThreadArgs->len);
        // if (new_socket_fd >= 0) {
        //     printf("New connection accepted on port %d\n", ThreadArgs->port);

        //     char buffer[MAX_BUFFER_SIZE];
        //     read(new_socket_fd, buffer, MAX_BUFFER_SIZE);
            
        //     if (strncmp("exit", buffer, 4) == 0) {
        //         system("clear");

        //         Sensor_data data = get_data(shared);

        //         //Ghi log closed
        //         char log_message[MAX_BUFFER_SIZE];
        //         snprintf(log_message, sizeof(log_message), "The sensor node with %d has closed the connection\n", data.SensorNodeID);
        //         log_events(log_message);
        //         close(new_socket_fd);
        //         break;
        //     }

        //     // Giải mã dữ liệu từ sensor
        //     Sensor_data data;
        //     sscanf(buffer, "%d %f", &data.SensorNodeID, &data.temperature);

        //     //Thêm dữ liệu vào shared buffer
        //     add_data(shared, data);

        //     //Ghi log new connection
        //     char log_message[MAX_BUFFER_SIZE];
        //     snprintf(log_message, sizeof(log_message), "A sensor node with %d has opened a new connection\n", data.SensorNodeID);
        //     log_events(log_message);
        //     close(new_socket_fd);
        // }}
    }
    // close(new_socket_fd);
    close(server_fd);
    close(epoll_fd);
    return NULL;
}

//Data manager thread
static void *thr_data(void *args) {

    printf("i'm thread data\n");
    while (1) 
    {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&cond, &lock);
    
        Shared_data *shared = (Shared_data *)args;
        Sensor_data data = get_data(shared);

        if (data.SensorNodeID !=0 ) {
            char log_message[MAX_BUFFER_SIZE];
            if (data.temperature > 30.0) {
                snprintf(log_message, sizeof(log_message), "The sensor node with %d reports it’s too hot (running avg temperature = %.1f)\n", data.SensorNodeID, data.temperature);
                log_events(log_message);
            } else if (data.temperature < 15) {
                snprintf(log_message, sizeof(log_message), "The sensor node with %d reports it’s too cold (running avg temperature = %.1f)\n", data.SensorNodeID, data.temperature);
                log_events(log_message);
            }
        }
        printf("nodeid: %d\n", data.SensorNodeID);
        printf("temperature: %.2f\n", data.temperature);
        pthread_mutex_unlock(&lock);
    }
}

//Storage manager thread
static void *thr_storage(void *args) {
    printf("i'm thread storage\n");
    

    while (1) {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&cond, &lock);
        static int b = 0;
        b++;

        Shared_data *shared = (Shared_data *)args;
        
        sqlite3 *db;        //Con trỏ cơ sở dữ liệu
        char *errMsg = 0;   //Thông báo mã lỗi
        int rc;             //Mã trả về từ SQLite
        const char *sql;    //Câu lệnh SQL
        Sensor_data data = get_data(shared);
        if (data.SensorNodeID != 0) {
            //Ghi dữ liệu vào SQL database
            char log_message[MAX_BUFFER_SIZE];
            rc = sqlite3_open(SQL_database, &db); //Mở hoặc tạo database
            
            if ((rc != SQLITE_OK) & (b <= 1)) {
                snprintf(log_message, sizeof(log_message), "Connection to SQL server lost.\n");
                log_events(log_message);
            } else if ((rc == SQLITE_OK) & (b <= 1)) {
                snprintf(log_message, sizeof(log_message), "Connection to SQL server established.\n");
                log_events(log_message);
            }
            //Thực thi câu lệnh SQL
            if (b <= 1) {
                sql =   "CREATE TABLE IF NOT EXISTS Sensor_data (" \
                        "SENSOR_ID INTERGER PRIMARY KEY," \
                        "Temperature FLOAT );";

                //Tạo bảng
                rc = sqlite3_exec(db, sql, NULL, 0, &errMsg);
                if (rc != SQLITE_OK) {
                    snprintf(log_message, sizeof(log_message), "Created new table %s failed.\n", SQL_database);
                    log_events(log_message);
                } else {
                    snprintf(log_message, sizeof(log_message), "New table %s created.\n",SQL_database);
                    log_events(log_message);
                }
            }

            //Chèn dữ liệu vào bảng
            char sql_query[MAX_BUFFER_SIZE];
            snprintf(sql_query, sizeof(sql_query),
                    "INSERT OR REPLACE INTO Sensor_data (SENSOR_ID, Temperature) VALUES (%d, %.2f);", data.SensorNodeID, data.temperature);
            // snprintf(sql_query, sizeof(sql_query),
            //         "MERGE INTO Sensor_data AS target" \
            //         "USING (SELECT %d AS SENSOR_ID, %.2f AS Temperature) AS source" \
            //         "ON target.SENSOR_ID = source.SENSOR_ID" \
            //         "WHEN MATCHED THEN" \
            //         "   UPDATE SET Temperature = source.Temperature" \
            //         "WHEN NOT MATCHED THEN" \
            //         "   INSERT (SENSOR_ID, Temperature)" \
            //         "   VALUES (source.SENSOR_ID, source.Temperature);", 
            //         data.SensorNodeID, data.temperature );
            rc = sqlite3_exec(db, sql_query, NULL, 0, &errMsg);
            if (rc != SQLITE_OK) {
                snprintf(log_message, sizeof(log_message), "Data insertion failed\n");
                log_events(log_message);
            } else {
                snprintf(log_message, sizeof(log_message), "Successful data insertion\n");
                log_events(log_message);
            }
            pthread_mutex_unlock(&lock);

            sqlite3_close(db);
        }
    }
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

int main(int argc, char *argv[])
{
    /* Bỏ qua tín hiệu SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

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