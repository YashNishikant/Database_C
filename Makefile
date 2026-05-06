CC = gcc
CFLAGS = -g -Wall -std=c99 -fsanitize=address,undefined
TARGET = db

# Object files
OBJS = db.o b_tree.o types.o

# Default rule
all: $(TARGET)

# Link step
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

# Compile db.c
db.o: db.c b_tree.h types.h
	$(CC) $(CFLAGS) -c db.c

# Compile btree.c
b_tree.o: b_tree.c b_tree.h types.h
	$(CC) $(CFLAGS) -c b_tree.c

# Compile types.c
types.o: types.c types.h
	$(CC) $(CFLAGS) -c types.c

clean:
	rm -f $(OBJS) $(TARGET)