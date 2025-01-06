#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>     //  Chứa cấu trúc cần thiết cho socket. 
#include <netinet/in.h>     //  Thư viện chứa các hằng số, cấu trúc khi sử dụng địa chỉ trên internet
#include <arpa/inet.h>
#include <unistd.h>

#define MAX_BUFF_SIZE 1024

/* Chức năng gửi dữ liệu*/
void data_func(int server_fd, int SensorNodeID)
{
    int numb_write;
    char sendbuff[MAX_BUFF_SIZE];
    char temp_input[MAX_BUFF_SIZE];
    float temperature;

    while (1) {
        memset(sendbuff, '0', MAX_BUFF_SIZE);
 
        printf("Please enter the temperature: ");
        fgets(temp_input, MAX_BUFF_SIZE, stdin);

        /* Chuyển đổi chuỗi thành số thực */
        if (sscanf(temp_input, "%f", &temperature) != 1) {
            printf("Invalid input. Please enter a valid temperature.\n");
            continue;
        }

        snprintf(sendbuff, sizeof(sendbuff),"%d %.2f", SensorNodeID, temperature);

        /* Gửi thông tin đến server bằng hàm write */
        numb_write = write(server_fd, sendbuff, strlen(sendbuff));
        if (numb_write == -1) {
            perror("write failed");
            exit(EXIT_FAILURE);
        }
        if (strncmp("exit", temp_input, 4) == 0) {
            printf("Client exit ...\n");
            break;
        }
    }
    close(server_fd);
}

int main(int argc, char *argv[])
{
    int portno;
    int SensorNodeID;
    int server_fd;
    struct sockaddr_in serv_addr;

    /* Gắn giá trị 0 vào các byte của biến serv_fd */
    memset(&serv_addr, '0', sizeof(serv_addr));

    /* Đọc port & SensorNodeID từ command line */
    if (argc < 4) {
        printf("Command: ./client <server address> <port number> <SensorNodeID>\n");
        exit(1);
    }
    portno = atoi(argv[2]);
    SensorNodeID = atoi(argv[3]);

    /* Khởi tạo địa chỉ server */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) == -1) {
        perror("inet_pton failed");
        exit(EXIT_FAILURE);
    }

    /* Tạo socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /* Kết nối tới server */
    if (connect(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("connect failed");
        exit(EXIT_FAILURE);
    }

    data_func(server_fd, SensorNodeID);

    return 0;
}