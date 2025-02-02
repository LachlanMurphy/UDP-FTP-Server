/* 
 * udpserver.c - A simple UDP echo server 
 * usage: udpserver <port>
 */

// Author: Lachlan Murphy
// 2 February 2025

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/time.h>
#include <errno.h>

#define BUFSIZE 1024

/*
 * error - wrapper for perror
 */
void error(char *msg) {
	perror(msg);
	exit(1);
}

// sends a packet to an adress
int sendPacket(char* buf, int len, int sockfd, struct sockaddr_in * clientaddr, int clientlen, int byte_num);

// gets a packet
int getPacket(char* buf, int sockfd, struct sockaddr_in * clientaddr, int clientlen, int byte_num);


int main(int argc, char **argv) {
	int sockfd; /* socket */
	int portno; /* port to listen on */
	int clientlen; /* byte size of client's address */
	struct sockaddr_in serveraddr; /* server's addr */
	struct sockaddr_in clientaddr; /* client addr */
	struct hostent *hostp; /* client host info */
	char buf[BUFSIZE]; /* message buf */
	char *hostaddrp; /* dotted decimal host addr string */
	int optval; /* flag value for setsockopt */
	int n; /* message byte size */

	/* 
	* check command line arguments 
	*/
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	portno = atoi(argv[1]);

	/* 
	* socket: create the parent socket 
	*/
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) error("ERROR opening socket");

	/* setsockopt: Handy debugging trick that lets 
	* us rerun the server immediately after we kill it; 
	* otherwise we have to wait about 20 secs. 
	* Eliminates "ERROR on binding: Address already in use" error. 
	*/
	optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

	/*
	* build the server's Internet address
	*/
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)portno);

	/* 
	* bind: associate the parent socket with a port 
	*/
	if (bind(sockfd, (struct sockaddr *) &serveraddr, 
		sizeof(serveraddr)) < 0) 
			error("ERROR on binding");

	/* 
	* main loop: wait for a datagram, then echo it
	*/
	clientlen = sizeof(clientaddr);

	// keeps track of which packet number we should be recieving
	int send_byte_order;
	int get_byte_order;
	while (1) {

		/*
		* recvfrom: receive a UDP datagram from a client
		*/
		bzero(buf, BUFSIZE);
		send_byte_order = 0;
		get_byte_order = 0;
		n = getPacket(buf, sockfd, &clientaddr, clientlen, get_byte_order++);
		printf("%s\n", (char *) &clientaddr.sin_addr.s_addr);
		/* 
		* gethostbyaddr: determine who sent the datagram
		*/
		hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, sizeof(clientaddr.sin_addr.s_addr), AF_INET);
		if (hostp == NULL) error("ERROR on gethostbyaddr");

		hostaddrp = inet_ntoa(clientaddr.sin_addr);
		if (hostaddrp == NULL) error("ERROR on inet_ntoa\n");

		printf("server received datagram from %s (%s)\n", hostp->h_name, hostaddrp);
		printf("server received %ld/%d bytes: %s\n", strlen(buf), n, buf);
		
		// determine what to do with the message
		if (!strncmp(buf, "exit", strlen("exit"))) {
			// send EXIT ACK to client
			strncpy(buf, "EXIT", sizeof(buf));
			sendPacket(buf, strlen(buf), sockfd, &clientaddr, clientlen, send_byte_order++);
			sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen, send_byte_order++);
		} else if (!strncmp(buf, "ls", strlen("ls"))) {
			// get all files in current directory
			DIR *d;
			struct dirent *dir;
			d = opendir(".");
			if (d) {
				while ((dir = readdir(d)) != NULL) {
					// the space is added here as compared to the client adding it
					int len = strlen(dir->d_name);
					memset(dir->d_name+len, ' ', 1);
					dir->d_name[len+1] = '\0';
					sendPacket(dir->d_name, strlen(dir->d_name), sockfd, &clientaddr, clientlen, send_byte_order++);
				}
				closedir(d);
				sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen, send_byte_order++);
			}
		} else if (!strncmp(buf, "get", strlen("get"))) {
			// get file name if exists
			char file_name[256];
			int delimiter = strcspn(buf, " \n\0");
			strncpy(file_name, buf+delimiter+1, strlen(buf+delimiter+1)+1);

			printf("Checking if %s exists...\n", file_name);
			if (access(file_name, F_OK)) {
				// file does not exist
				printf("File %s does not exist.\n", file_name);
				sendPacket("NOFILE", strlen("NOFILE"), sockfd, &clientaddr, clientlen, send_byte_order++);
				sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen, send_byte_order++);
			} else {
				printf("File exists. Sending %s\n", file_name);
				// send file
				FILE* file = fopen(file_name, "r");
				int n = 0;
				while (1) {
					n = fread(buf, 1, BUFSIZE, file);
					if (n <= 0) break;

					sendPacket(buf, n, sockfd, &clientaddr, clientlen, send_byte_order++);
				}
				sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen, send_byte_order++);
				fclose(file);
			}
		} else if (!strncmp(buf, "put", strlen("put"))) {
			// extract file name
			char file_name[256];
			int delimiter = strcspn(buf, " \n\0");
			strncpy(file_name, buf+delimiter+1, strlen(buf+delimiter+1)+1);

			// create file
			FILE* file = fopen(file_name, "w");
			if (!file) {
				error("Error creating new file (PUT)");
			}

			// create timeout for client response
			struct timeval tv;
			tv.tv_sec = 0;
			tv.tv_usec = 500000;
			if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
				error("ERROR in setsockopt");
				exit(-1);
			}

			int ack = 0;
			// start listening for packets
			// loop because there may be multiple packets
			while (1) {
				// clear buffer
				bzero(buf, BUFSIZE);

				// send ack if not already sent
				if (!ack) {
					sendPacket("PUT_ACK", strlen("PUT_ACK"), sockfd, &clientaddr, clientlen, send_byte_order++);
					ack = 1;
				}

				// await response
				n = getPacket(buf, sockfd, &clientaddr, clientlen, get_byte_order++);

				// check if err or timeout occured
				if (n < 0) {
					// check if timoeut
					if (errno == EAGAIN || errno == EWOULDBLOCK) {
						// timeout reached
						fprintf(stderr, "Client Timed out\n");
						break;
					} else {
						// other err
						error("ERROR in recvfrom");
					}
				} else {
					// check if END is recieved
					if (!strncmp(buf, "END", strlen("END"))) {
						fclose(file);

						// send END back to client
						sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen, send_byte_order++);
						break;
					}

					// write to file
					fwrite(buf, n, 1, file);
				}
			}

			// reset socket timeout
			tv.tv_usec = 0;
			if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
				error("ERROR in setsockopt");
				exit(-1);
			}
		} else if (!strncmp(buf, "delete", strlen("delete"))) {
			// extract file name
			char file_name[256];
			int delimiter = strcspn(buf, " \n\0");
			strncpy(file_name, buf+delimiter+1, strlen(buf+delimiter+1)+1);

			// check if file exists
			if (!access(file_name, F_OK)) {
				// file exists
				if (!remove(file_name)) {
					sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen, send_byte_order++);
				} else {
					sendPacket("DELETE_ERR", strlen("DELETE_ERR"), sockfd, &clientaddr, clientlen, send_byte_order++);
				}
			} else {
				sendPacket("DELETE_ERR", strlen("DELETE_ERR"), sockfd, &clientaddr, clientlen, send_byte_order++);
			}
		} else {
			// command does not exist, shouldn't happen but
			// its best to put the edge case here
			sendPacket("BADINPT", strlen("BADINPT"), sockfd, &clientaddr, clientlen, send_byte_order++);
		}
	}
}

// sends a packet, resends if all bytes were not sent
int sendPacket(char* buf, int len, int sockfd, struct sockaddr_in * clientaddr, int clientlen, int byte_num) {
	char ack_buf[256]; // buffer to get ack
	char send_buf[BUFSIZE+4]; // extra four bytes to hold packet number
	int count = 0; // # of times we try to resend the data

	// init send buffer
	memcpy(send_buf, buf, len);

	// add byte number to buffer
	send_buf[len++] = (char) (byte_num >> 3);
	send_buf[len++] = (char) (byte_num >> 2);
	send_buf[len++] = (char) (byte_num >> 1);
	send_buf[len++] = (char) (byte_num);

	// set timeout for recvfrom
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 500000; // half a seocond
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
		error("ERROR in setsockopt");
	}

	// for debugging
	// printf("Sending packet %d %s\n", byte_num, send_buf);

	// send packet
	send_packet_lbl:
	sendto(sockfd, send_buf, len, 0, (struct sockaddr *) clientaddr, clientlen);
	
	// wait for ACK from other computer
	socklen_t socklen = sizeof(clientaddr);
	int n = recvfrom(sockfd, ack_buf, sizeof(ack_buf), 0, (struct sockaddr *) &clientaddr, &socklen);
	
	if (n < 0) {
		// check if timoeut
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			// timeout reached
			// could have been a packet loss, retry unless too many
			if (++count > 5) {
				tv.tv_usec = 0;
				if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
					error("ERROR in setsockopt");
				}
				return -1;
			}
			
			goto send_packet_lbl;
		} else {
			// other err
			error("ERROR in recvfrom");
		}
	}

	// make sure the ACK was recieved
	if (!strncmp(ack_buf, "GEN_ACK", strlen("GEN_ACK"))) {
		tv.tv_usec = 0;
		if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
			error("ERROR in setsockopt");
		}
	}
	
	// reset socket timeout
	tv.tv_usec = 0;
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
		error("ERROR in setsockopt");
	}

	return n;
}

int getPacket(char* buf, int sockfd, struct sockaddr_in * clientaddr, int clientlen, int byte_num) {
	int r_byte_num; // recieving pack #
	int n; // # of bytes recieved
	char get_buf[BUFSIZE+4]; // buffer to get data
	do {
		n = recvfrom(sockfd, get_buf, BUFSIZE + 4, 0, (struct sockaddr *) clientaddr, (socklen_t *) &clientlen);
		
		if (n < 0) {
			// check if timoeut
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				printf("Server timed out\n");
				return -1;
			} else {
				// other err
				error("ERROR in recvfrom");
			}
		}

		// send ack
		sendto(sockfd, "GEN_ACK", strlen("GEN_ACK"), 0, (struct sockaddr *) clientaddr, clientlen);
		
		// get byte number from packet
		r_byte_num = (get_buf[n-4] << 3) | (get_buf[n-3] << 2) | (get_buf[n-2] << 1) | get_buf[n-1];
		
		// for debugging
		// printf("%s Got packet with number %d == %d with %d\n", get_buf, r_byte_num, byte_num, n);
	
	} while (r_byte_num != byte_num);

	// copy over actual data to given buffer
	memcpy(buf, get_buf, n-4);
	
	return n-4; // to correct for the byte number, no longer needed
}