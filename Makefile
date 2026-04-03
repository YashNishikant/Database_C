# Makefile
CC = gcc
# The target executable
TARGET = db
# Source file
SRC = db.c
# Flags
CFLAGS = -std=c99 -g -Wall -fsanitize=address,undefined
# Default rule
all: $(TARGET)
# Rule to build the target
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

# Clean up
clean:
	rm -f $(TARGET)