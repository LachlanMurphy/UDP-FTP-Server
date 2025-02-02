/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <errno.h>

#define BUFSIZE 1024

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

// keep track of what kind of message was requested
enum Message_t {
	GET = 0,
	PUT = 1,
	DELETE = 2,
	LS = 3,
	EXIT = 4,
	NONE = -1
};

// sends a packet to an adress
int sendPacket(char* buf, int len, int sock_fd, struct sockaddr_in * clientaddr, int clientlen);

// sends a file to the server
int sendFile();

int main(int argc, char **argv) {
    int sockfd, portno, n;
    int serverlen;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char buf[BUFSIZE];

    /* check command line arguments */
    if (argc != 3) {
		fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
		exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    /* get a message from the user */
    get_usr: // return label for when previous request completes
    bzero(buf, BUFSIZE);
    printf("Please enter msg: ");
    fgets(buf, BUFSIZE, stdin);

    // change ending character to \0
    buf[strcspn(buf, "\n")] = '\0';

    // get file name if it exists
    char file_name[256];
    // if usr gets multiple files in one session this is helpful
    bzero(file_name, 256);
    int delimiter = strcspn(buf, " \n");
    strncpy(file_name, buf+delimiter+1, strlen(buf+delimiter+1));

    // verify the syntax is correct and set message type
    int type = NONE;

    // in the case we are PUT/GET, we need a fd for that file
    FILE* rw_fd = NULL;

    if (!strncmp(buf, "ls", strlen("ls"))) {
      	type = LS;
    } else if (!strncmp(buf, "delete", strlen("delete"))) {
      	type = DELETE;
    } else if (!strncmp(buf, "exit", strlen("exit"))) {
      	type = EXIT;
    } else if (!strncmp(buf, "put", strlen("put"))) {
      	type = PUT;

		// check if file exists
		if (access(file_name, F_OK)) {
			printf("File %s does not exist.\n", file_name);
			goto get_usr;
		}

		// if file exists open as read
		if ((rw_fd = fopen(file_name, "r")) == NULL) {
			error("ERROR with fopen (PUT)");
		}
    } else if (!strncmp(buf, "get", strlen("get"))) {
		type = GET;

		// open new file as write
		if ((rw_fd = fopen(file_name, "w")) == NULL) {
			error("ERROR with fopen (GET)");
		}
    } else {
		// non valid input
		printf("Incorrect Input\n");
		goto get_usr;
    }

    /* send the message to the server */
    serverlen = sizeof(serveraddr);
    n = sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr *) &serveraddr, serverlen);
    if (n < 0) error("ERROR in sendto");

    
    // set timeout
    struct timeval tv;
    tv.tv_sec = 2; // 2 second time out
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
		error("ERROR in setsockopt");
		exit(-1);
    }

    // likely multiple packets will be sent
    // loop through recieve function until
    // break code END is recieved
    while (1) {
		// reset buffer
		bzero(buf, BUFSIZE);
		
		n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *) &serverlen);

		// check if err or timeout occured
		if (n < 0) {

			// check if timoeut
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// timeout reached
				fprintf(stderr, "Client Timed out\n");
				goto get_usr;
			} else {
				// other err
				error("ERROR in recvfrom");
			}
		} else {
			// packet recieved

			// if EXIT code recieved then exit the program
			if (!strcmp(buf, "EXIT")) {
				// Exit process
				printf("%s\n", buf);
				exit(0);
			} else {
				// if end signal given, end seeking for this stream of packets
				if (!strcmp(buf, "END")) {
					// run what each message type's end
					switch (type) {
						case LS: {
							printf("\n");
						} break;
						case GET: {
							printf("File %s successfully retrieved\n", file_name);
							fclose(rw_fd);
						} break;
						case PUT: {
							printf("File %s sent.\n", file_name);
							fclose(rw_fd);
						} break;
						case DELETE: {
							printf("File %s deleted from server.\n", file_name);
						} break;
					}
				
					break; // break out of packet seek loop
				}
			
				// check what type the original request was, then process
				switch (type) {
					case LS: {
						// just print the file name that was recieved
						printf("%s", buf);
					} break;
					case GET: {
						// check if NOFILE flag was sent
						if (!strncmp(buf, "NOFILE", strlen("NOFILE"))) {
							printf("No such file exists on the server.\n");
							goto get_usr;
						} else {
							// write to local file
							fwrite(buf, n, 1, rw_fd);
						}
					} break;
					case PUT: {
						// all sent data should be handled inside one loop call
						if (strncmp(buf, "PUT_ACK", strlen("PUT_ACK"))) {
							// this should theoretically never happen but could
							printf("Error with server's response.\n");
							goto get_usr;
						} else {
							// send data to server
							// integrity of file has already been checked
							sendFile(rw_fd, buf, sockfd, &serveraddr, serverlen);
						}
					} break;
					case DELETE: {
						if (!strncmp(buf, "DELETE_ERR", strlen("DELETE_ERR"))) {
							printf("Unable to delete %s from server.\n", file_name);
							goto get_usr;
						}
					} break;
				}
			}
		}
    }
    goto get_usr;
    return 0;
}

// sends a packet, resends if all bytes were not sent
int sendPacket(char* buf, int len, int sock_fd, struct sockaddr_in * clientaddr, int clientlen) {
	int n = 0;
	
	while (n < len){
		int sent = sendto(sock_fd, buf+n, len-n, 0, (struct sockaddr *) clientaddr, clientlen);
		if (sent < 0) error("ERROR in sendto");

		n += sent;
	}

	return n;
}

int sendFile(FILE* file, char* buf, int sockfd, struct sockaddr_in * clientaddr, int clientlen) {

	// n keeps track of how many bytes we have sent so far in the file
	int n = 0;
	while (1) {
		n = fread(buf, 1, BUFSIZE, file);
		if (n <= 0) break;

		sendPacket(buf, n, sockfd, clientaddr, clientlen);
	}
	sendPacket("END", strlen("END"), sockfd, clientaddr, clientlen);\
	return n;
}