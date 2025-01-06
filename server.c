#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include <sys/socket.h>     //  Chứa cấu trúc cần thiết cho socket. 
#include <netinet/in.h>     //  Thư viện chứa các hằng số, cấu trúc khi sử dụng địa chỉ trên internet
#include<arpa/inet.h>
#include<pthread.h>
#include<fcntl.h>
#include<string.h>
#include<time.h>
#include<sqlite3.h> 

#define MAX_BUFFER_SIZE     1024
#define FIFO_FILE           "./logFIFO"
#define LOG_FILE            "./gateway.log"
#define SQL_database        "database.db"

pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;

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
    fd = open(FIFO_FILE, O_WRONLY | O_CREAT, 0666);

    if (fd == -1) {
        printf("call open() failed\n");
    } else {
        write(fd, message, strlen(message));
    }
    pthread_mutex_unlock(&log_lock);
    close(fd);
}

//Hàm ghi vào gateway.log
void wr_log(void *args) {
    static int a = 0;
    a++;
    //a là sequence_number

    char *message = (char *)args;

    FILE *log = fopen(LOG_FILE, "a");

    time_t current_time = time(NULL);
    struct tm *time_info = localtime(&current_time);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);

    fprintf(log, "%d. %s %s\n", a, timestamp, message);
    fflush(log);
    fclose(log);
}

//Add data into buffer
void add_data(Shared_data *shared, Sensor_data data) {
    pthread_mutex_lock(&data_lock);

    shared->buffer[shared->tail] = data;
    shared->tail = (shared->tail + 1) % MAX_BUFFER_SIZE;

    pthread_mutex_unlock(&data_lock);
}

//Get data from buffer
Sensor_data get_data(Shared_data *shared) {
    Sensor_data data = {0};
    pthread_mutex_lock(&data_lock);
        data = shared->buffer[shared->head];
        shared->head = (shared->head + 1) % MAX_BUFFER_SIZE;
    pthread_mutex_unlock(&data_lock);
    return data;
}

//Connection manager thread
static void *thr_connection(void *args) {
    Thread_args *ThreadArgs = (Thread_args *)args;
    Shared_data *shared = ThreadArgs->shared_data;

    int opt = 0;
    int server_fd, new_socket_fd;
    struct sockaddr_in servaddr, sensoraddr;
    
    //Gắn giá trị 0 vào các byte của servaddr, sensoraddr
    memset(&servaddr, 0, sizeof(struct sockaddr_in));
    memset(&sensoraddr, 0, sizeof(struct sockaddr_in));

    //Tạo socket TCP
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket failed\n");
        exit(EXIT_FAILURE);
    }

    //Ngăn lỗi: "address already in use"
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    //Khởi tạo địa chỉ cho cổng
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(ThreadArgs->port);

    //Gắn socket vào cổng
    if (bind(server_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) == -1) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    //Lấy thông tin sensor
    ThreadArgs->len = sizeof(sensoraddr);

    printf("Connection Manager listening on port %d\n", ThreadArgs->port);

    while (1) {
        new_socket_fd = accept(server_fd, (struct sockaddr *)&sensoraddr, (socklen_t *)&ThreadArgs->len);
        if (new_socket_fd >= 0) {
            printf("New connection accepted on port %d\n", ThreadArgs->port);

            char buffer[MAX_BUFFER_SIZE];
            read(new_socket_fd, buffer, MAX_BUFFER_SIZE);
            
            if (strncmp("exit", buffer, 4) == 0) {
                system("clear");

                Sensor_data data = get_data(shared);

                //Ghi log closed
                char log_message[MAX_BUFFER_SIZE];
                snprintf(log_message, sizeof(log_message), "The sensor node with %d has closed the connection\n", data.SensorNodeID);
                log_events(log_message);
                close(new_socket_fd);
                break;
            }

            //Giải mã dữ liệu từ sensor
            Sensor_data data;
            sscanf(buffer, "%d %f", &data.SensorNodeID, &data.temperature);

            //Thêm dữ liệu vào shared buffer
            add_data(shared, data);

            //Ghi log new connection
            char log_message[MAX_BUFFER_SIZE];
            snprintf(log_message, sizeof(log_message), "A sensor node with %d has opened a new connection\n", data.SensorNodeID);
            log_events(log_message);
            close(new_socket_fd);
        }
    }
    close(new_socket_fd);
    close(server_fd);
}

//Data manager thread
static void *thr_data(void *args) {
    Shared_data *shared = (Shared_data *)args;

    while (1)
    {
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
    }
    
}

//Storage manager thread
static void *thr_storage(void *args) {
    
    static int b = 0;
    b++;

    Shared_data *shared = (Shared_data *)args;
    
    sqlite3 *db;        //Con trỏ cơ sở dữ liệu
    char *errMsg = 0;   //Thông báo mã lỗi
    int rc;             //Mã trả về từ SQLite
    const char *sql;    //Câu lệnh SQL

    while (1) {
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
            sql = "INSERT INTO Sensor_data (SENSOR_ID, Temperature) VALUES (data.SensorNodeID, data.temperature)";
            rc = sqlite3_exec(db, sql, NULL, 0, &errMsg);
            if (rc != SQLITE_OK) {
                snprintf(log_message, sizeof(log_message), "Data inserted fail\n");
                log_events(log_message);
            } else {
                snprintf(log_message, sizeof(log_message), "Data inserted succeessfully\n");
                log_events(log_message);
            }

            sqlite3_close(db);
        }
    }
}

//Child process
void log_process() {
    printf("Im log process. My PID: %d\n", getpid());

    mkfifo(FIFO_FILE, 0666);
    int fd;
    char buff[MAX_BUFFER_SIZE];

    fd  = open(FIFO_FILE, O_RDONLY);

    while(1) {
        ssize_t bytes = read(fd, buff, sizeof(buff) - 1);
        if (bytes > 0) {
            wr_log(&buff);
        }
    }

    close(fd);
    exit(0);
}

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

        while(1);
        wait(NULL);
    }

    pthread_join(threadconn, NULL);
    pthread_join(threaddata, NULL);
    pthread_join(threadstorage, NULL);

    return 0;
}