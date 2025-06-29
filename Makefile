CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
TARGET = ftp_server
SOURCE = ftp_server.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET) ./test_files

.PHONY: all clean run 