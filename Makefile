CC = gcc
FLAGS = -g -Wall -Wextra
DEPS = header.h

SRC_DIR = src
OBJ_DIR = obj
OUT_DIR = out

OBJ_DEPS = $(OBJ_DIR)/common.o $(OBJ_DIR)/crc32.o
SERVER_O = $(OBJ_DIR)/server.o
CLIENT_O = $(OBJ_DIR)/client.o


SOURCES := $(wildcard $(SRC_DIR)/*.c)
HEADERS := $(wildcard $(SRC_DIR)/header.h)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

all: setup server client

setup:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(OUT_DIR)

$(OBJECTS): $(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	$(CC) $(FLAGS) -c $< -o $@

server: $(SERVER_O) $(OBJ_DEPS)
	$(CC) $(SERVER_O) $(OBJ_DEPS) $(FLAGS) -o $(OUT_DIR)/server

client: $(CLIENT_O) $(OBJ_DEPS)
	$(CC) $(CLIENT_O) $(OBJ_DEPS) $(FLAGS) -o $(OUT_DIR)/client

clean:
	rm -rf $(OBJ_DIR) $(OUT_DIR)
