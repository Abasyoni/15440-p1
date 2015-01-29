#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

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

char* replyToClient(optype op,int returnValue, int errorNumber, int *size) {
    printf("Generating reply.\n");
    *size = sizeof(para)+sizeof(opHeader);
    para *p = malloc(sizeof(para));
    p->a = returnValue;
    p->b = errorNumber;
    opHeader* h = malloc(sizeof(opHeader));
    h->type = op;
    h->size= sizeof(para);
    char* msg = malloc (sizeof(para)+ sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, sizeof(para));
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
            printf("Got header: type: %d, size: %zd\n", hdr->type, hdr->size);
            para* buf1 = malloc(hdr->size);
            switch (hdr->type) {
                case KOPENOP:
                    while ( (rv=recv(sessfd, buf1, hdr->size, 0)) > 0) {
                        para *p = buf1;
                        printf("Got para: flags: %d, mode: %d, filepath: %s", p->a, p->b, p->s);
                        printf("\n");
                        
                        const char* filepath = p->a;
                        open(filepath, p->a, p->b);
                        

                        // send reply
                        printf("Sending reply. \n");
                        int mSize = 0;
                        char *msg = replyToClient(KOPENOP,0,1,&mSize);
                        send(sessfd, msg, mSize, 0);	// should check return value
                    }
                    break;
                    
                case KWRITEOP:
                    while ( (rv=recv(sessfd, buf1, hdr->size, 0)) > 0) {
                        para *p = buf1;
                        printf("Got para: fd: %d, buf: %s, nbyte: %d", p->a, p->s, p->b);
                        printf("\n");

                        // send reply
                        printf("Sending reply. \n");
                        int mSize = 0;
                        char *msg = replyToClient(KWRITEOP,2,3,&mSize);
                        send(sessfd, msg, mSize, 0);	// should check return value
                    }
                    break;
                    
                case KCLOSEOP:
                    while ( (rv=recv(sessfd, buf1, hdr->size, 0)) > 0) {
                        para *p = buf1;
                        printf("Got para: fd: %d", p->a);
                        printf("\n");

                        // send reply
                        printf("Sending reply. \n");
                        int mSize = 0;
                        char *msg = replyToClient(KCLOSEOP,4,5,&mSize);
                        send(sessfd, msg, mSize, 0);	// should check return value
                    }
                    break;
                    
                default:
                    break;
            }
        }


		// either client closed connection, or error
		if (rv<0) err(1,0);
		close(sessfd);
	}

	printf("server shutting down cleanly\n");
	// close socket
	close(sockfd);

	return 0;
}

