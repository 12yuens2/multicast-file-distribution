CC = gcc
FLAGS = -g
DEPS = header.h

OBJ_DEPS = common.o crc32.o

%.o: %.c $(DEPS)
	$(CC) $(FLAGS) -c $< -o $@

all: server client

server: server.o $(OBJ_DEPS)
	$(CC) server.o $(OBJ_DEPS) $(FLAGS) -o server

client: client.o $(OBJ_DEPS)
	$(CC) client.o $(OBJ_DEPS) $(FLAGS) -o client

clean:
	rm -f *.o server client
