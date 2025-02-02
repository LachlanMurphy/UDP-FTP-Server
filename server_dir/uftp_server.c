/* 
 * udpserver.c - A simple UDP echo server 
 * usage: udpserver <port>
 */

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
int sendPacket(char* buf, int len, int sock_fd, struct sockaddr_in * clientaddr, int clientlen);


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
	while (1) {

		/*
		* recvfrom: receive a UDP datagram from a client
		*/
		bzero(buf, BUFSIZE);
		n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, (socklen_t *) &clientlen);
		if (n < 0) error("ERROR in recvfrom");

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
			sendPacket(buf, strlen(buf), sockfd, &clientaddr, clientlen);
			sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen);
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
					sendPacket(dir->d_name, strlen(dir->d_name), sockfd, &clientaddr, clientlen);
				}
				closedir(d);
				sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen);
			}
		} else if (!strncmp(buf, "get", strlen("get"))) {
			// get file name if exists
			char file_name[256];
			int delimiter = strcspn(buf, " \n\0");
			strncpy(file_name, buf+delimiter+1, strlen(buf+delimiter+1)+1);

			printf("Checking if %s exists...\n", file_name);
			if (access(file_name, F_OK)) {
				// file does not exist
				sendPacket("NOFILE", strlen("NOFILE"), sockfd, &clientaddr, clientlen);
				sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen);
			} else {
				// send file
				FILE* file = fopen(file_name, "r");
				int n = 0;
				while (1) {
					n = fread(buf, 1, BUFSIZE, file);
					if (n <= 0) break;

					sendPacket(buf, n, sockfd, &clientaddr, clientlen);
				}
				sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen);
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
			tv.tv_sec = 2; // 2 second time out
			tv.tv_usec = 0;
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
					sendPacket("PUT_ACK", strlen("PUT_ACK"), sockfd, &clientaddr, clientlen);
					ack = 1;
				}

				// await response
				n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, (socklen_t *) &clientlen);

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
						sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen);
						break;
					}

					// write to file
					fwrite(buf, n, 1, file);
				}
			}

			// reset socket timeout
			tv.tv_sec = 0;
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
					sendPacket("END", strlen("END"), sockfd, &clientaddr, clientlen);
				} else {
					sendPacket("DELETE_ERR", strlen("DELETE_ERR"), sockfd, &clientaddr, clientlen);
				}
			} else {
				sendPacket("DELETE_ERR", strlen("DELETE_ERR"), sockfd, &clientaddr, clientlen);
			}
		} else {
			// command does not exist, shouldn't happen but
			// its best to put the edge case here
			sendPacket("BADINPT", strlen("BADINPT"), sockfd, &clientaddr, clientlen);
		}
	}
}

// sends a packet, resends if all bytes were not sent
int sendPacket(char* buf, int len, int sock_fd, struct sockaddr_in * clientaddr, int clientlen) {
	int n = 0;
	int count = 0;
	while (n < len){
		printf("%d\n", ++count);
		int sent = sendto(sock_fd, buf+n, len-n, 0, (struct sockaddr *) clientaddr, clientlen);
		if (sent < 0) error("ERROR in sendto");

		n += sent;
	}

	return n;
}