#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <err.h>


#define MAXMSGLEN 100

// The following line declares a function pointer with the same prototype as the open function.
int (*orig_open)(const char *pathname, int flags, ...);  // mode_t mode is needed when flags includes O_CREAT


int sendToServer (char* msg) {
	char *serverip;
	char *serverport;
	unsigned short port;
	char buf[MAXMSGLEN+1];
	int sockfd, rv;
	struct sockaddr_in srv;

	// Get environment variable indicating the ip address of the server
	serverip = getenv("server15440");
	if (serverip) printf("Got environment variable server15440: %s\n", serverip);
	else {
		printf("Environment variable server15440 not found.  Using 127.0.0.1\n");
		serverip = "127.0.0.1";
	}

	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (serverport) fprintf(stderr, "Got environment variable serverport15440: %s\n", serverport);
	else {
		fprintf(stderr, "Environment variable serverport15440 not found.  Using 15440\n");
		serverport = "15440";
	}
	port = (unsigned short)atoi(serverport);

	// Create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
	if (sockfd<0) err(1, 0);			// in case of error

	// setup address structure to point to server
	memset(&srv, 0, sizeof(srv));			// clear it first
	srv.sin_family = AF_INET;			// IP family
	srv.sin_addr.s_addr = inet_addr(serverip);	// IP address of server
	srv.sin_port = htons(port);			// server port

	// actually connect to the server
	rv = connect(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
	if (rv<0) err(1,0);

	// send message to server
	printf("client sending to server: %s\n", msg);
	send(sockfd, msg, strlen(msg), 0);	// send message; should check return value

	// get message back
	rv = recv(sockfd, buf, MAXMSGLEN, 0);	// get message
	if (rv<0) err(1,0);			// in case something went wrong
	buf[rv]=0;				// null terminate string to print
	printf("client got messge: %s\n", buf);

	// close socket
	close(sockfd);

	return 0;
}

// This is our replacement for the open function from libc.
int open(const char *pathname, int flags, ...) {
	mode_t m=0;
	if (flags & O_CREAT) {
		va_list a;
		va_start(a, flags);
		m = va_arg(a, mode_t);
		va_end(a);
	}
	// we just print a message, then call through to the original open function (from libc)
	fprintf(stderr, "mylib: open called for path %s\n", pathname);
    sendToServer("open");
	return orig_open(pathname, flags, m);
}

// This function is automatically called when program is started
void _init(void) {
	// set function pointer orig_open to point to the original open function
	orig_open = dlsym(RTLD_NEXT, "open");
	fprintf(stderr, "Init mylib\n");
}


