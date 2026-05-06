# # Makefile
# CC = gcc
# # The target executable
# TARGET = db
# # Source files
# SRC = db.c b_tree.c
# # Flags
# CFLAGS = -std=c99 -g -Wall -fsanitize=address,undefined
# # Default rule
# all: $(TARGET)

# # Rule to build the target
# db: db.c b_tree.c
# 	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) -lm

# db.c: types.h

# # Clean up
# clean:
# 	rm -f $(TARGET)

CC = gcc
CFLAGS = -g -Wall -std=c99 -fsanitize=address,undefined

all: db

db: db.o 
	$(CC) $(CFLAGS) $^ -o $@

db.o: types.h
