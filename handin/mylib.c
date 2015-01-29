#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>

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

#include "dirtree.h"

#define MAXMSGLEN 100
#define RETURNSIZE (sizeof(opHeader)+sizeof(para))

// The following line declares a function pointer with the same prototype as the open function.
int (*orig_open)(const char *pathname, int flags, ...);  // mode_t mode is needed when flags includes O_CREAT
int (*orig_close)(int fildes);
ssize_t (*orig_read)(int fildes, void *buf, size_t nbyte);
ssize_t (*orig_write)(int fildes, const void *buf, size_t nbyte);
off_t (*orig_lseek)(int fd, off_t offset, int whence);
int (*orig_stat)(int ver, const char * path, struct stat * stat_buf);
int (*orig_unlink)(const char *path);
ssize_t (*orig_getdirentries)(int fd, char *buf, size_t nbytes , off_t *basep);

struct dirtreenode* (*orig_getdirtree)( const char *path );
void (*orig_freedirtree)( struct dirtreenode* dt );

/* Definition of a parameter struct */
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

para* sendToServer (void* msg, size_t size) {
	char *serverip;
	char *serverport;
	unsigned short port;
    char buf[MAXMSGLEN];
	int sockfd, rv;
	struct sockaddr_in srv;

	// Get environment variable indicating the ip address of the server
	serverip = getenv("server15440");
	if (!serverip) {
		serverip = "127.0.0.1";
	}

	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (!serverport) {
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
	send(sockfd, msg, size, 0);	// send message; should check return value

	// get message back
	rv = recv(sockfd, buf, RETURNSIZE, 0);	// get message
    opHeader* hdr = malloc(sizeof(opHeader));
    memcpy(hdr, buf, sizeof(opHeader));
    printf("Got back header: type: %d, size: %zd\n", hdr->type, hdr->size);
    para* buf1 = malloc(hdr->size);
    memcpy(buf1, buf+sizeof(opHeader), hdr->size);
    printf("Got return: %d, error num: %d", buf1->a, buf1->b);
    printf("\n");
    free(hdr);
    
	if (rv<0) err(1,0);			// in case something went wrong
	buf[rv]=0;				// null terminate string to print

	// close socket
	orig_close(sockfd);
    

	return buf1;
}

para* serialize_open(const char* pathname, int flags, int mode, int* size) {
    size_t strSize = sizeof(pathname);
    *size = sizeof(para) + strSize;
    para* p = malloc(sizeof(para) + strSize);
    p->a = flags;
    p->b = 0;
    stpcpy(p->s, pathname);
    return p;
}


para* serialize_write(int fd, const void *buf, size_t nbyte, int* size) {
    size_t strSize = nbyte;
    *size = sizeof(para) + strSize;
    para* p = malloc(sizeof(para) + strSize);
    p->a = fd;
    p->b = nbyte;
    stpcpy(p->s, (char*)buf);
    return p;
}

para* serialize_close(int fd, int *size) {
    *size = sizeof(para);
    para* p = malloc(sizeof(para));
    p->a = fd;
    p->b = 0;
    return p;
}

// This is our replacement for the open function from libc.
int open(const char *pathname, int flags, ...) {
    printf("open!\n");
	mode_t m=0;
	if (flags & O_CREAT) {
		va_list a;
		va_start(a, flags);
		m = va_arg(a, mode_t);
		va_end(a);
	}
	// we just print a message, then call through to the original open function (from libc)
    int psize = 0;
    para *p = serialize_open(pathname, flags, m, &psize);
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KOPENOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    sendToServer(msg, (psize+sizeof(opHeader)));
    free(h);
    free(msg);
    free(p);
	return orig_open(pathname, flags, m);
}


ssize_t read(int fd, void *buf, size_t nbyte) {
    printf("read!\n");
	return orig_read(fd, buf, nbyte);
}

ssize_t write(int fd, const void *buf, size_t nbyte) {
    printf("write!\n");
    int psize = 0;
    const void* buf1 = buf;
    para *p = serialize_write(fd, buf1, nbyte, &psize);
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KWRITEOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    sendToServer(msg, (psize+sizeof(opHeader)));
    free(h);
    free(msg);
    free(p);
//    sendToServer("write", 6);
	return orig_write(fd, buf, nbyte);
}

int close(int fd) {
    printf("close!\n");
    int psize = 0;
    para *p = serialize_close(fd, &psize);
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KCLOSEOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    sendToServer(msg, (psize+sizeof(opHeader)));
    free(h);
    free(msg);
    free(p);
//    sendToServer("close", 6);
	return orig_close(fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    sendToServer("lseek", 10);
    return orig_lseek(fd, offset, whence);
}

int __xstat(int ver, const char * path, struct stat * stat_buf) {
    sendToServer("stat", 10);
    return orig_stat(ver, path, stat_buf);
}

int unlink(const char *path) {
    sendToServer("unlink", 10);
    return orig_unlink(path);
}

ssize_t getdirentries(int fd, char *buf, size_t nbytes , off_t *basep) {
    sendToServer("getdirentries", 10);
    return orig_getdirentries(fd, buf, nbytes, basep);
}

struct dirtreenode* getdirtree( const char *path ) {
    sendToServer("getdirtree", 10);
    return orig_getdirtree(path);
}

void freedirtree( struct dirtreenode* dt ) {
    sendToServer("freedirtree", 10);
    return orig_freedirtree(dt);
}

// This function is automatically called when program is started
void _init(void) {
	// set function pointer orig_open to point to the original open function
	orig_open  = dlsym(RTLD_NEXT, "open");
	orig_close = dlsym(RTLD_NEXT, "close");
	orig_read  = dlsym(RTLD_NEXT, "read");
	orig_write = dlsym(RTLD_NEXT, "write");
    orig_lseek = dlsym(RTLD_NEXT, "lseek");
    orig_stat  = dlsym(RTLD_NEXT, "__xstat");
    orig_unlink = dlsym(RTLD_NEXT, "unlink");
    orig_getdirentries = dlsym(RTLD_NEXT, "getdirentries");
    orig_getdirtree = dlsym(RTLD_NEXT, "getdirtree");
    orig_freedirtree = dlsym(RTLD_NEXT, "freedirtree");

	fprintf(stderr, "Init mylib\n");
}


