#include<stdio.h>
#include<sqlite3.h>
#include"storage_manager.h"
#include"log.h"
#include"handler_sensor.h"

/* Storage manager thread */
void *thr_storage(void *args) {
    printf("i'm thread storage\n");
    Shared_data *shared = (Shared_data *)args;
    sqlite3 *db;        // Databases pointer
    char *errMsg = 0;   // Error issue message
    int rc;             // Return issue from SQLite
    const char *sql;    // Statement SQL
    char log_message[MAX_BUFFER_SIZE];
    char sql_query[MAX_BUFFER_SIZE];

    rc = sqlite3_open(SQL_database, &db); // Open or create database
    if (rc != SQLITE_OK) 
    {
        snprintf(log_message, sizeof(log_message),
                "Connection to SQL server lost.\n");
        log_events(log_message);
    } 
    else if (rc == SQLITE_OK) 
    {
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
    if (rc != SQLITE_OK) 
    {
        snprintf(log_message, sizeof(log_message),
                "Created new table %s failed.\n", SQL_database);
        log_events(log_message);
    } 
    else 
    {
        snprintf(log_message, sizeof(log_message),
                "New table %s created.\n",SQL_database);
        log_events(log_message);
    }

    while (1) 
    {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&cond, &lock);
        Sensor_data data;
        while (1)
        {
            if (shared->handler_counter) 
            {
                data = get_data(shared);
                break;
            }
        }
        pthread_mutex_unlock(&lock);

        if (data.SensorNodeID != 0) 
        {
            //insert or replace sensor's data into SQL database
            snprintf(sql_query, sizeof(sql_query),
                    "INSERT OR REPLACE INTO Sensor_data (SENSOR_ID, Temperature) VALUES (%d, %.2f);",
                    data.SensorNodeID, data.temperature);
            rc = sqlite3_exec(db, sql_query, NULL, 0, &errMsg);
            
            if (rc != SQLITE_OK) 
            {
                snprintf(log_message, sizeof(log_message), "Data insertion failed\n");
                log_events(log_message);
            } 
            else 
            {
                snprintf(log_message, sizeof(log_message), "Successful data insertion\n");
                log_events(log_message);
            }
        }
    }
    sqlite3_close(db);
}