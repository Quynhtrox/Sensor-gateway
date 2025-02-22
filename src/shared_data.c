#include "shared_data.h"

// Initialize global variables here (only one copy)
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;