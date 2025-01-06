PRJ_NAME = SenSor_System
FIFO_FILE = logFIFO
LOG_FILE = gateway.log
SQLite = database.db

all:
	gcc -o server server.c -lpthread -lsqlite3
	gcc -o client client.c

clean:
	rm -rf server client $(SQLite) $(FIFO_FILE) $(LOG_FILE)