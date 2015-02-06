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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>

#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <dirent.h>

#include <netinet/tcp.h>

#include "../include/dirtree.h"


#define MAXMSGLEN 100

typedef struct {
    size_t size;
    int type;
} opHeader;

typedef struct {
    int a;
    int b;
    int c;
    off_t o;
    char s[];
} para;


typedef enum {
    KOPENOP,
    KCLOSEOP,
    KREADOP,
    KWRITEOP,
    KLSEEKOP,
    KSTATOP,
    KUNLINKOP,
    KGETDIRENTRIESOP,
    KGETDIRTREEOP,
    KFREEDIRTREE
} optype;

char* replyToClient(optype op,int returnValue, int errorNumber, int passBack,
                    char* buf, int bufLen,int *size) {
    int strSize = 0;
    *size = sizeof(para)+sizeof(opHeader)+strlen(buf)+1;
    strSize = bufLen+1;
    para *p = malloc(sizeof(para)+strSize);
    p->a = returnValue;
    p->b = errorNumber;
    p->c = passBack;
    memcpy(p->s, (void*)buf, strSize);
//    p->s[bufLen] = '\0';
    opHeader* h = malloc(sizeof(opHeader));
    h->type = op;
    h->size= sizeof(para)+strSize;
//    printf("In replyToClient: p size: %d\n", h->size);
    char* msg = malloc (sizeof(para) + strSize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, sizeof(para)+strSize);
    free(p);
    free(h);

    return msg;
}


void serverOpen(para *p, int sessfd){
    const char* filepath = p->s;
//         printf("OPEN pathname: %s, flags: %d\n", filepath, p->a); 
    int ret = open(filepath, p->a, p->b);
//                        printf("    fd = %d, errno=%d\n", ret, errno);
    // send reply
//                        printf("    Sending reply. \n");
    int mSize = 0;
    char *msg = replyToClient(KOPENOP, ret, errno, 0,"", 0,&mSize);
    send(sessfd, msg, mSize, 0);	// should check return value
    free(msg);
}

void serverWrite(para * p, int sessfd){
    const void* writeBuf = p->s;
//         printf("    WRITE fd: %d, nbytes: %d\n", p->a, p->b);
    int ret = write(p->a, writeBuf, p->b);

    // send reply
//                        printf("    Sending reply. \n");
    int mSize = 0;
    char *msg = replyToClient(KWRITEOP, ret, errno, 0, "",0,&mSize);
    send(sessfd, msg, mSize, 0);	// should check return value
    free(msg);
}

void serverRead(para * p, int sessfd){
    int nbyte = p->b;
    para* readBuf = malloc(sizeof(para) + nbyte);
//         printf("    Read fd: %d, nbytes: %d \n", p->a, p->b);
    int ret = read(p->a, (void*) readBuf->s, nbyte);
    readBuf->a = ret;
    readBuf->b = errno;
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KREADOP;
    h->size= sizeof(para)+nbyte;
    char* msg = malloc (sizeof(para) + nbyte + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), readBuf, sizeof(para)+nbyte);

    // send reply
//                        printf("    Sending reply. \n");
    send(sessfd, msg, (sizeof(para) + nbyte + sizeof(opHeader)), 0);	// should check return value
    free(readBuf);
    free(h);
    free(msg);
}

void serverClose(para * p, int sessfd){
    int ret = close(p->a);

    // send reply
//                        printf("Sending reply. \n");
    int mSize = 0;
    char *msg = replyToClient(KCLOSEOP, ret, errno, 0, "",0,&mSize);
    send(sessfd, msg, mSize, 0);	// should check return value
    free(msg);
}

void serverLSeek(para * p, int sessfd){
    int ret = lseek(p->a, p->b, p->c);

    // send reply
//                        printf("Sending reply. \n");
    int mSize = 0;
    char *msg = replyToClient(KLSEEKOP, ret, errno, 0, "",0,&mSize);
    send(sessfd, msg, mSize, 0);	// should check return value
    free(msg);
}

void serverStat(para * p, int sessfd){
    int pathLen = p->b;
    char *path = malloc(pathLen);
    memcpy(path, p->s, pathLen);
    const char* conPath = path;
    int ret = __xstat(p->a, conPath, (struct stat *)(p->s+pathLen));

    // send reply
//                        printf("Sending reply. \n");
    int mSize = 0;
    char *msg = replyToClient(KSTATOP, ret, errno, 0, "",0,&mSize);
    send(sessfd, msg, mSize, 0);	// should check return value
    free(path);
    free(msg);
}

void serverUnlink(para * p, int sessfd){
    const void* path = p->s;
    int ret = unlink(path);

    // send reply
//                        printf("Sending reply. \n");
    int mSize = 0;
    char *msg = replyToClient(KUNLINKOP, ret, errno, 0, "",0,&mSize);
    send(sessfd, msg, mSize, 0);	// should check return value
    free(msg);
}

void serverGetdirentries(para * p, int sessfd){
    off_t base = p->o;
    int nbytes = p->b;
//    printf("base: %ld, nbytes: %d\n", base, nbytes);
    char* getdirBuf = malloc(nbytes);
    off_t *basep = (&base);
    
    int ret = getdirentries(p->a, getdirBuf, nbytes, basep);
    
    int newBase = (int) (*basep);
//    printf("newbase: %d\n", newBase);
    
    // send reply
//                        printf("Sending reply. \n");
    int mSize = 0;
    char *msg = replyToClient(KGETDIRENTRIESOP, ret, errno, newBase,
                              getdirBuf, strlen(getdirBuf), &mSize);
//        printf("    msize: %d\n", mSize);
    send(sessfd, msg, mSize, 0);	// should check return value
    free(getdirBuf);
    free(msg);
}

void serverGetdirtree(para * p, int sessfd){
    const void* path = p->s;
    
    struct dirtreenode* treeRoot = getdirtree(path);
//        struct dirtreenode* flatRoot = flatten(treeRoot);
    treeRoot = NULL;

    // send reply
//                        printf("Sending reply. \n");
    int mSize = 0;
    char *msg = replyToClient(KGETDIRTREEOP, 0, errno, 0, "",0,&mSize);
    send(sessfd, msg, mSize, 0);	// should check return value
    free(msg);
}

void handleClient (int sessfd) {
    int rv;
    
	// get messages and send replies to this client, until it goes away
    opHeader* hdr = malloc(sizeof(opHeader));
    
    while ( (rv=recv(sessfd, hdr, sizeof(opHeader), 0)) > 0) {
//        printf("Got header: type: %d, size: %zd\n", hdr->type, hdr->size);
        char* buf1 = malloc(hdr->size);
        int recieved = 0;
        while (recieved < (hdr->size)) {
            rv=recv(sessfd, (((char *)buf1) + recieved), (hdr->size), 0);
            recieved += rv;
        }
        
//        printf("\n\n\nheader size: %d\n", hdr->size);
        
        para * p = (para*) buf1;
        
        switch (hdr->type) {
            case KOPENOP:
//                printf("open!\n");
                serverOpen(p, sessfd);
                break;
            case KCLOSEOP:
//                printf("close!\n");
                serverClose(p, sessfd);
                break;
            case KREADOP:
//                printf("read!\n");
                serverRead(p, sessfd);
                break;
            case KWRITEOP:
//                printf("write!\n");
                serverWrite(p, sessfd);
                break;
            case KLSEEKOP:
//                printf("lseek!\n");
                serverLSeek(p, sessfd);
                break;
            case KSTATOP:
//                printf("stat!\n");
                serverStat(p, sessfd);
                break;
            case KUNLINKOP:
//                printf("unlink!\n");
                serverUnlink(p, sessfd);
                break;
            case KGETDIRENTRIESOP:
//                printf("Get dir entries!\n");
                serverGetdirentries(p, sessfd);
//                printf("Get dir entries finished!\n");
                break;
            case KGETDIRTREEOP:
                serverGetdirtree(p, sessfd);
                break;
            case KFREEDIRTREE:
                break;
            default:
                break;
        }
        
        free(p);
    }
    free(hdr);
    
}

int main(int argc, char**argv) {
//	char buf[MAXMSGLEN+1];
	char *serverport;
	unsigned short port;
	int sockfd, sessfd, rv;
	struct sockaddr_in srv, cli;
	socklen_t sa_size;

	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (serverport) port = (unsigned short)atoi(serverport);
//	else port=15440;
	else port=34335;

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
        
        if (fork() == 0) {
            close(sockfd);
            handleClient(sessfd);
            exit(0);
        }
        
		// either client closed connection, or error
		close(sessfd);
	}

	printf("server shutting down cleanly\n");
	// close socket
	close(sockfd);

	return 0;
}

