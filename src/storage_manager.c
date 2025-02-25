#include<stdio.h>
#include<sqlite3.h>
#include"storage_manager.h"
#include"log.h"
#include"handler_sensor.h"
#include"wait_sensor_signal.h"

void create_db_table(sqlite3 *db);
void send_data(sqlite3 *db, Sensor_data data);

/* Storage manager thread */
void *thr_storage(void *args) {
    Shared_data *shared = (Shared_data *)args;
    sqlite3 *db;

    // Open database
    int rc = sqlite3_open(SQL_database, &db);
    if (rc != SQLITE_OK) {
        printf("Cannot open database: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    create_db_table(db);
    while (1) 
    {
        Sensor_data data;
        data = wait_sensor_signal(shared);
        send_data(db, data);
    }
    sqlite3_close(db);
}

/* Create database.db table */
void create_db_table(sqlite3 *db){
    const char *sql;    // Statement SQL
    char *errMsg = 0;   // Error issue message
    int rc;             // Return issue from SQLite
    char log_message[MAX_BUFFER_SIZE];

    if (db == NULL) {
        snprintf(log_message, sizeof(log_message), "Database connection is NULL, cannot create table.\n");
        log_events(log_message);
        return;
    }
    
    // SQL statement execution
    sql =   "CREATE TABLE IF NOT EXISTS Sensor_data (" \
            "SENSOR_ID INTEGER PRIMARY KEY," \
            "Temperature FLOAT );";
            
    // Create data table
    rc = sqlite3_exec(db, sql, NULL, 0, &errMsg);
    if (rc != SQLITE_OK) {
        snprintf(log_message, sizeof(log_message),
                "Created new table %s failed.\n", SQL_database);
        log_events(log_message);
        sqlite3_free(errMsg);
    } 
    else {
        snprintf(log_message, sizeof(log_message),
                "New table %s created.\n",SQL_database);
        log_events(log_message);
    }
}

/* Send data into database.db */
void send_data(sqlite3 *db, Sensor_data data){
    int rc;             // Return issue from SQLite
    char *errMsg = 0;   // Error issue message
    char log_message[MAX_BUFFER_SIZE];
    char sql_query[MAX_BUFFER_SIZE];

    if (db == NULL) {
        snprintf(log_message, sizeof(log_message), "Database connection is NULL, cannot insert data.\n");
        log_events(log_message);
        return;
    }

    //insert or replace sensor's data into SQL database
    snprintf(sql_query, sizeof(sql_query),
            "INSERT OR REPLACE INTO Sensor_data (SENSOR_ID, Temperature) VALUES (%d, %.2f);",
            data.SensorNodeID, data.temperature);
    rc = sqlite3_exec(db, sql_query, NULL, 0, &errMsg);
    
    if (rc != SQLITE_OK) {
        snprintf(log_message, sizeof(log_message), "Data insertion failed: %s\n", sqlite3_errmsg(db));
        log_events(log_message);
    } 
    else {
        snprintf(log_message, sizeof(log_message), "Successful data insertion\n");
        log_events(log_message);
    }
}