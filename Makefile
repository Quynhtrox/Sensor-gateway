PRJ_NAME = SenSor_System

.PHONY: make_dir create_obj

CUR_DIR := .
INC_DIR := $(CUR_DIR)/inc
SRC_DIR := $(CUR_DIR)/src
OBJ_DIR := $(CUR_DIR)/obj

CC := gcc
C_FLAGS := -c -fPIC -I$(INC_DIR)
LFLAGS := -lpthread -lsqlite3

DATA_FILE = logFIFO gateway.log database.db

SERVER = server
SENSOR = sensor

OBJ_FILES := $(OBJ_DIR)/shared_data.o $(OBJ_DIR)/log.o $(OBJ_DIR)/handler_sensor.o $(OBJ_DIR)/connection_manager.o $(OBJ_DIR)/data_manager.o $(OBJ_DIR)/storage_manager.o $(OBJ_DIR)/wait_sensor_signal.o

# Create dir
make_dir:
	mkdir -p $(OBJ_DIR)

# Create object files
create_obj:
	$(CC) $(C_FLAGS) -o $(OBJ_DIR)/shared_data.o $(SRC_DIR)/shared_data.c
	$(CC) $(C_FLAGS) -o $(OBJ_DIR)/wait_sensor_signal.o $(SRC_DIR)/wait_sensor_signal.c
	$(CC) $(C_FLAGS) -o $(OBJ_DIR)/log.o $(SRC_DIR)/log.c
	$(CC) $(C_FLAGS) -o $(OBJ_DIR)/handler_sensor.o $(SRC_DIR)/handler_sensor.c
	$(CC) $(C_FLAGS) -o $(OBJ_DIR)/connection_manager.o $(SRC_DIR)/connection_manager.c
	$(CC) $(C_FLAGS) -o $(OBJ_DIR)/data_manager.o $(SRC_DIR)/data_manager.c
	$(CC) $(C_FLAGS) -o $(OBJ_DIR)/storage_manager.o $(SRC_DIR)/storage_manager.c
	$(CC) $(C_FLAGS) -o $(CUR_DIR)/server.o $(CUR_DIR)/server.c
	$(CC) $(C_FLAGS) -o $(CUR_DIR)/sensor.o $(CUR_DIR)/sensor.c

# Build Server
$(SERVER) : $(CUR_DIR)/server.o $(OBJ_FILES)
	$(CC) $^ -o $@ $(LFLAGS)

# Build Sensor
$(SENSOR) : $(CUR_DIR)/sensor.o
	$(CC) $^ -o $@ $(LFLAGS)

# Build all
all: make_dir create_obj $(SERVER) $(SENSOR)

# Clean
clean:
	rm -rf $(DATA_FILE) $(SENSOR) $(SERVER) $(OBJ_DIR) $(CUR_DIR)/*.o
