# Makefile
CC = gcc
# The target executable
TARGET = db
# Source files
SRC = db.c b_tree.c
# Flags
CFLAGS = -std=c99 -g -Wall -fsanitize=address,undefined
# Default rule
all: $(TARGET)

# Rule to build the target
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) -lm

# Clean up
clean:
	rm -f $(TARGET)