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
#include <errno.h>

#include <netinet/tcp.h>

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

int curSockFD;

para* sendToServer (void* msg, size_t size) {
    char buf[MAXMSGLEN];
	int rv;

    // Turning off Nagle's
    int flag = 1;
    int result = setsockopt(curSockFD,            /* socket affected */
                            IPPROTO_TCP,     /* set option at TCP level */
                            TCP_NODELAY,     /* name of option */
                            (char *) &flag,  /* the cast is historical cruft */
                            sizeof(int));    /* length of option value */
//    if (result < 0)
//        printf("Failed to turn off Nagle's algorithim.");
    
	// send message to server
	send(curSockFD, msg, size, 0);	// send message; should check return value

	// get message back
	rv = recv(curSockFD, buf, sizeof(opHeader), 0);	// get message
    opHeader* hdr = malloc(sizeof(opHeader));
    memcpy(hdr, buf, sizeof(opHeader));
    char* buf1 = malloc(hdr->size);
//    fprintf(stderr,"Recieving return value of size: %zd\n", hdr->size);
    if (((hdr->type) == KREADOP) || ((hdr->type) == KSTATOP)) {
        int recieved = 0;
        while (recieved < (hdr->size)) {
            rv = recv(curSockFD, ((char*)buf1)+recieved, (hdr->size), 0);
            recieved += rv;
    //        fprintf(stderr, "    Recieved: %d\n", recieved);
        }
    } else {
        rv = recv(curSockFD, buf1, hdr->size,0);
    }
    free(hdr);

	if (rv<0) err(1,0);			// in case something went wrong

	return (para*) buf1;
}

para* serialize_open(const char* pathname, int flags, int mode, int* size) {
    size_t strSize = strlen(pathname) + 1;
    *size = sizeof(para) + strSize;
    para* p = malloc(sizeof(para) + strSize);
    p->a = flags;
    p->b = mode;
    strncpy(p->s, pathname, strSize);
    return p;
}


para* serialize_write(int fd, const void *buf, size_t nbyte, int* size) {
    size_t strSize = nbyte+1;
    *size = sizeof(para) + strSize;
    para* p = malloc(sizeof(para) + strSize);
    p->a = fd;
    p->b = nbyte;
    memcpy(p->s, buf, nbyte);
    /* strncpy(p->s, (char*)buf, nbyte); */
    return p;
}

para* serialize_read(int fd, size_t nbyte, int *size) {
    *size = sizeof(para);
    para* p = malloc(sizeof(para));
    p->a = fd;
    p->b = nbyte;
    return p;
}

para* serialize_close(int fd, int *size) {
    *size = sizeof(para);
    para* p = malloc(sizeof(para));
    p->a = fd;
    p->b = 0;
    return p;
}

para* serialize_lseek(int fd, off_t offset, int whence,int *size) {
    *size = sizeof(para);
    para* p = malloc(sizeof(para));
    p->a = fd;
    p->b = offset;
    p->c = whence;
    return p;
}

para* serialize_stat(int ver, const char * path,
                     struct stat * stat_buf, int*size) {
    size_t strSize = strlen(path)+sizeof(struct stat)+1;
    *size = sizeof(para) + strSize;
    para* p = malloc(sizeof(para) + strSize);
    p->a = ver;
    p->b = strlen(path)+1;
    memcpy(p->s, path, p->b);
    memcpy(p->s+p->b, stat_buf, sizeof(struct stat));
    /* strncpy(p->s, (char*)buf, nbyte); */
    return p;
}

para* serialize_unlink(const char *path, int* size) {
    size_t strSize = strlen(path)+1;
    *size = sizeof(para) + strSize;
    para* p = malloc(sizeof(para) + strSize);
    p->a = strlen(path);
    memcpy(p->s, path, strSize);
    /* strncpy(p->s, (char*)buf, nbyte); */
    return p;
}

para* serialize_getdirentries(int fd, size_t nbytes, off_t* basep, int* size) {
    *size = sizeof(para);
    para* p = malloc(sizeof(para));
    p->a = fd;
    p->b = nbytes;
    p->c = *basep;
    p->o = *basep;
    return p;
}

para* serialize_getdirtree(const char *path, int* size) {
    size_t strSize = strlen(path)+1;
    *size = sizeof(para) + strSize;
    para* p = malloc(sizeof(para) + strSize);
    p->a = strlen(path);
    memcpy(p->s, path, strSize);
    /* strncpy(p->s, (char*)buf, nbyte); */
    return p;
}

// This is our replacement for the open function from libc.
int open(const char *pathname, int flags, ...) {
//    fprintf(stderr, "Open: %s\n", pathname);
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
    char* msg = malloc(psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    free(h);
    free(p);
    para* pRet = sendToServer(msg, (psize+sizeof(opHeader)));
    int ret = pRet->a;
    errno = pRet->b;
    free(pRet);
    free(msg);
	return ret;
}

ssize_t read(int fd, void *buf, size_t nbyte) {
//    if ((fd == 2)|| (fd == 1)|| (fd == 0)) {
//        return orig_read(fd, buf, nbyte);
//    }
//    fprintf(stderr,"read!\n");
    int psize = 0;
//     printf("    input fd: %d, nbytes: %zd\n", fd, nbyte);
    para *p = serialize_read(fd, nbyte, &psize);
    /* printf("    in para fd: %d, nbytes: %d, buf: %s\n", p->a, p->b, p->s); */
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KREADOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    para* pRet = sendToServer(msg, (psize+sizeof(opHeader)));
//    printf("    got reply: length %zd!\n", strlen(p->s));
    int ret = pRet->a;
    errno = pRet->b;
//    printf("    return val: %d, error: %s\n", pRet->a, strerror(pRet->b));
    memcpy(buf, pRet->s, nbyte);
    free(pRet);
    free(h);
    free(msg);
    free(p);
	return ret;
}

ssize_t write(int fd, const void *buf, size_t nbyte) {
//    fprintf(stderr,"Write!");
//     printf("    write fd: %d, nbytes: %zd\n", fd, nbyte);
//    if ((fd == 2)|| (fd == 1)|| (fd == 0)) {
//        return orig_write(fd, buf, nbyte);
//    }
    int psize = 0;
    const void* buf1 = buf;
    para *p = serialize_write(fd, buf1, nbyte, &psize);
    /* printf("    in para fd: %d, nbytes: %d, buf: %s\n", p->a, p->b, p->s); */
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KWRITEOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    para* pRet = sendToServer(msg, (psize+sizeof(opHeader)));
    int ret = pRet->a;
    errno = pRet->b;
    free(pRet);
    free(h);
    free(msg);
    free(p);
	return ret;
}

int close(int fd) {
//    if ((fd == 2)|| (fd == 1)|| (fd == 0)) {
//        orig_close(fd);
//    }
//     printf("close fd: %d\n", fd);
    int psize = 0;
    para *p = serialize_close(fd, &psize);
    /* printf("    in para fd: %d\n", p->a); */
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KCLOSEOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    para* pRet = sendToServer(msg, (psize+sizeof(opHeader)));
    int ret = pRet->a;
    errno = pRet->b;
    free(pRet);
    free(h);
    free(msg);
    free(p);
	return ret;
}

off_t lseek(int fd, off_t offset, int whence) {
    /* printf("close fd: %d\n", fd); */
    int psize = 0;
    para *p = serialize_lseek(fd, offset, whence, &psize);
    /* printf("    in para fd: %d\n", p->a); */
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KLSEEKOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    para* pRet = sendToServer(msg, (psize+sizeof(opHeader)));
    int ret = pRet->a;
    errno = pRet->b;
    free(pRet);
    free(h);
    free(msg);
    free(p);
	return ret;
}

int __xstat(int ver, const char * path, struct stat * stat_buf) {
    /* printf("close fd: %d\n", fd); */
    int psize = 0;
    para *p = serialize_stat(ver, path, stat_buf, &psize);
    /* printf("    in para fd: %d\n", p->a); */
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KSTATOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    para* pRet = sendToServer(msg, (psize+sizeof(opHeader)));
    int ret = pRet->a;
    errno = pRet->b;
    free(pRet);
    free(h);
    free(msg);
    free(p);
	return ret;
}

int unlink(const char *path) {
//     printf("unlink fd: %d\n", fd);
    int psize = 0;
    para *p = serialize_unlink(path, &psize);
    /* printf("    in para fd: %d\n", p->a); */
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KUNLINKOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    para* pRet = sendToServer(msg, (psize+sizeof(opHeader)));
    int ret = pRet->a;
    errno = pRet->b;
    free(pRet);
    free(h);
    free(msg);
    free(p);
	return ret;
}

ssize_t getdirentries(int fd, char *buf, size_t nbytes , off_t *basep) {
//     printf("getdirentries fd: %d\n", fd);
    int psize = 0;
    para *p = serialize_getdirentries(fd, nbytes, basep, &psize);
//     printf("    in para fd: %d, nbytes: %d, basep: %d\n", p->a, p->b, p->c);
    opHeader* h = malloc(sizeof(opHeader));
    // and here----------------------------------------
    h->type = KGETDIRENTRIESOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    para* pRet = sendToServer(msg, (psize+sizeof(opHeader)));
//    int ret = pRet->a;
    errno = pRet->b;
//    printf("new basep: %d", pRet->c);
    *basep = pRet->c;
    memcpy(buf, pRet->s, nbytes);
    free(pRet);
    free(h);
    free(msg);
    free(p);
	return -1;
//    printf("old base: %d\n", (int) *basep);
//    int ret = orig_getdirentries(fd, buf, nbytes, basep);
//    printf("new base: %d\n", (int) *basep);
//    return ret;
}

struct dirtreenode* getdirtree( const char *path ) {
    int psize = 0;
    para *p = serialize_getdirtree(path, &psize);
    /* printf("    in para fd: %d\n", p->a); */
    opHeader* h = malloc(sizeof(opHeader));
    h->type = KGETDIRTREEOP;
    h->size= psize;
    char* msg = malloc (psize + sizeof(opHeader));
    memcpy(msg, h, sizeof(opHeader));
    memcpy(msg+sizeof(opHeader), p, psize);
    para* pRet = sendToServer(msg, (psize+sizeof(opHeader)));
    int ret = pRet->a;
    errno = pRet->b;
    free(pRet);
    free(h);
    free(msg);
    free(p);
	return ret;
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

	char *serverip;
	char *serverport;
	unsigned short port;
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
//		serverport = "34735";
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

    curSockFD = sockfd;
    
}


void _fini(void) {
	// close socket
	orig_close(curSockFD);
}
