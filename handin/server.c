#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#define MAXMSGLEN 100

typedef struct {
    size_t size;
    int type;
} opHeader;

typedef struct {
    int a;
    int b;
    char s[];
} para;


typedef enum {
    KOPENOP,
    KCLOSEOP,
    KREADOP,
    KWRITEOP
} optype;

char* replyToClient(optype op,int returnValue, int errorNumber, char* buf,int *size) {
    *size = sizeof(para)+sizeof(opHeader)+strlen(buf)+1;
    int strSize = strlen(buf)+1;
    para *p = malloc(sizeof(para)+strSize);
    p->a = returnValue;
    p->b = errorNumber;
    strncpy(p->s, buf, strSize);
    opHeader* h = malloc(sizeof(opHeader));
    h->type = op;
    h->size= sizeof(para)+strSize;
    char* msg = malloc (sizeof(para) + strSize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, sizeof(para)+strSize);
    free(p);
    free(h);

    return msg;
}

int main(int argc, char**argv) {
	char buf[MAXMSGLEN+1];
	char *serverport;
	unsigned short port;
	int sockfd, sessfd, rv, i;
	struct sockaddr_in srv, cli;
	socklen_t sa_size;

	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (serverport) port = (unsigned short)atoi(serverport);
	else port=15440;

	// Create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
	if (sockfd<0) err(1, 0);			// in case of error

	// setup address structure to indicate server port
	memset(&srv, 0, sizeof(srv));			// clear it first
	srv.sin_family = AF_INET;			// IP family
	srv.sin_addr.s_addr = htonl(INADDR_ANY);	// don't care IP address
	srv.sin_port = htons(port);			// server port

	// bind to our port
	rv = bind(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
	if (rv<0) err(1,0);

	// start listening for connections
	rv = listen(sockfd, 5);
	if (rv<0) err(1,0);

	// main server loop, handle clients one at a time, quit after 10 clients
	while(1) {

		// wait for next client, get session socket
		sa_size = sizeof(struct sockaddr_in);
		sessfd = accept(sockfd, (struct sockaddr *)&cli, &sa_size);
		if (sessfd<0) err(1,0);

		// get messages and send replies to this client, until it goes away
        opHeader* hdr = malloc(sizeof(opHeader));
        if ( (rv=recv(sessfd, hdr, sizeof(opHeader), 0)) > 0) {
//            printf("Got header: type: %d, size: %zd\n", hdr->type, hdr->size);
            char* buf1 = malloc(hdr->size);
            switch (hdr->type) {
                case KOPENOP:
                    while ( (rv=recv(sessfd, buf1, hdr->size, 0)) > 0) {
                        para *p = (para*) buf1;

                        const char* filepath = p->s;
                        /* printf("OPEN pathname: %s, flags: %d\n", filepath, p->a); */
                        int ret = open(filepath, p->a, p->b);


                        // send reply
//                        printf("Sending reply. \n");
                        int mSize = 0;
                        char *msg = replyToClient(KOPENOP, ret, errno, NULL,&mSize);
                        send(sessfd, msg, mSize, 0);	// should check return value
                        free(msg);
                    }
                    break;

                case KWRITEOP:
                    while ( (rv=recv(sessfd, buf1, hdr->size, 0)) > 0) {
                        para *p = (para*) buf1;

                        const void* writeBuf = p->s;
                        /* printf("WRITE fd: %d, nbytes: %d, buf: %s\n", p->a, p->b, p->s); */
                        int ret = write(p->a, writeBuf, p->b);

                        // send reply
//                        printf("Sending reply. \n");
                        int mSize = 0;
                        char *msg = replyToClient(KWRITEOP, ret, errno, NULL,&mSize);
                        send(sessfd, msg, mSize, 0);	// should check return value
                        free(msg);
                    }
                    break;

                case KCLOSEOP:
                    while ( (rv=recv(sessfd, buf1, hdr->size, 0)) > 0) {
                        para *p = (para*) buf1;
                        int ret = close(p->a);

                        // send reply
//                        printf("Sending reply. \n");
                        int mSize = 0;
                        char *msg = replyToClient(KCLOSEOP, ret, errno, NULL,&mSize);
                        send(sessfd, msg, mSize, 0);	// should check return value
                        free(msg);
                    }
                    break;

                default:
                    break;
            }
            free(buf1);
        }
        free(hdr);

		// either client closed connection, or error
		if (rv<0) err(1,0);
		close(sessfd);
	}

	printf("server shutting down cleanly\n");
	// close socket
	close(sockfd);

	return 0;
}

