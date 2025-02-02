# Make file to compile server and client code
# Lachlan Murphy 2025

CC = gcc

CLIENT_CFLAGS = -g -Wall
SERVER_CFLAGS = -g -Wall

default: all

all: client server

client: client_dir/uftp_client.c
	$(CC) $(CLIENT_CFLAGS) -o client_dir/client client_dir/uftp_client.c

server: server_dir/uftp_server.c
	$(CC) $(SERVER_CFLAGS) -o server_dir/server server_dir/uftp_server.c

# clean: