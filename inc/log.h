#ifndef LOG_H
#define LOG_H

#include"shared_data.h"

void *log_events(void *args);
void wr_log(void *args, int log_fd);

#endif //LOG_H