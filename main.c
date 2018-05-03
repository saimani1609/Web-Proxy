#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
/*
 * 
 */
int writen(int fd, const void *vptr, int n);
int client_request(int sockfd, char*, int);
int getrequest(char*, char*, int*, int*, int*);
int findhost(char*, char*);
struct in_addr** calldns(char*);
int modifyrequest(char* request, int* requestp);
int readsocket(int fd, char *buf, int len, int timeout);

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Port number required.\n");
        exit(0);
    }

    signal(SIGPIPE, SIG_IGN);       /*Ignore the "Broken Pipe" error so the program will not terminate*/
    int listenfd, clifd, serfd;
    int nrecv, nrecvc, ncount = 0;
    socklen_t clilen;
    struct sockaddr_in cliaddr, servaddr;
    char buf[4096], clibuf[4096];
    char request[65536];
    int bufp = 0, requestp = 0, flag = 0, gr = 0;
    int rc;
    char badrequest[] = "HTTP/1.1 400 Bad request\r\n\r\n<html><body><h1>400 Bad request</h1>\nYour browser sent an invalid request.\n<body><html>\n";

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    bzero(&servaddr, sizeof (servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(atoi(argv[1]));
    bind(listenfd, (struct sockaddr*) &servaddr, sizeof (servaddr));
    if (listen(listenfd, 1024) == 0)
        printf("Listening %d...\n", (int) ntohs(servaddr.sin_port));

    while (1) {
        clilen = sizeof (cliaddr);
        clifd = accept(listenfd, (struct sockaddr*) &cliaddr, &clilen);
        printf("Connection accepted. fd=%d\n", clifd);

        /*Grab one request from buffer*/
        requestp = 0;
        bufp = 0;
        /*I thought that there could be multiple requests coming in one connection*/
        /*so I tried to mark the position where a request ends in the buffer. But*/
        /*it seems that it never happens, so bufp and flag might actually be useless.*/
        do {
            if (bufp == 0) {
                if ((nrecvc = read(clifd, clibuf, 4096)) <= 0) {
                    requestp = 0;
                    printf("No request found.\n");
                    break;
                }
            }
            gr = getrequest(clibuf, request, &bufp, &requestp, &flag); /*Returns 1 when request is incomplete and need to read again*/
        } while (gr > 0);

        /*Handle request*/
        if (requestp != 0) {
            request[requestp] = '\0';
            modifyrequest(request, &requestp);
            printf("\n\n---------- Client Request ----------\n");
            write(2, request, requestp);
            printf("-------------- End --------------\n\n");
            if ((serfd = client_request(clifd, request, requestp)) == -1) {
                printf("Unable to process request.\n");
                writen(clifd, badrequest, 118);
            } else {
                printf("Client request sent.\n");
                ncount = 0;
                while (1) {
                    nrecv = readsocket(serfd, buf, 4096, 3);
                    if (nrecv <= 0) break;
                    ncount = ncount + nrecv;
                    printf("Bytes received ---- %d\r", ncount);
                    rc = writen(clifd, buf, nrecv);
                    //               printf(" rc = %d, nrecv=%d\n", rc, nrecv);
                    if (rc <= 0)
                        break;
                }
                printf("\n");
                printf("Server connection closed.\n");
                close(serfd);
            }
        }
        close(clifd);
        printf("Client connection closed.\n\n\n=====================================\n\n\n");
    }
    return (EXIT_SUCCESS);
}

int client_request(int sockfd, char* request, int requestp) {
    ssize_t n;
    char hostname[64];

    struct in_addr **addr;
    int i = 0;
    int newfd;
    struct sockaddr_in newaddr;
    if (findhost(request, hostname)) {
        if ((addr = calldns(hostname)) != NULL) {
            for (i = 0; addr[i] != NULL; i++) {          /*Try IP addresses one by one*/
                printf("Address %d --- %s\n", i + 1, inet_ntoa(*addr[i]));
                bzero(&newaddr, sizeof (newaddr));
                newaddr.sin_family = AF_INET;
                newaddr.sin_port = htons(80);
                newaddr.sin_addr = *addr[i];
                if ((newfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                    printf("Fail to create socket for destination server.\n");
                    continue;
                }
                if (connect(newfd, (struct sockaddr*) &newaddr, sizeof (newaddr)) < 0) {
                    close(newfd);
                    printf("Unable to connect.\n");
                    continue;
                }
                printf("Server connected. fd=%d\n", newfd);
                if (writen(newfd, request, requestp) <= 0) {
                    close(newfd);
                    printf("Unable to send request.\n");
                    continue;
                }
                return newfd;
            }
            return -1;
        } else {
            printf("Unable to get address.\n");
            return -1;
        }
    } else {
        printf("Hostname not found.\n");
        return -1;
    }
}

int getrequest(char* buf, char *request, int *bufp, int *requestp, int *flag) {
    char get[] = "GET";
    char eor[] = "\r\n\r\n";
    char *p1 = buf + *bufp, *p2, *req = request;
    if (*flag == 0)
        if ((p1 = strstr(p1, get)) == NULL) {
            *bufp = 0;
            *flag = 0;
            return 0;
        }
    if ((p2 = strstr(p1, eor)) != NULL) {
        req = req + (*requestp);
        *requestp = *requestp + (p2 - p1 + 4);
        if (*requestp > 65535) {
            *requestp = 0;
            return -1;
        }
        memcpy(req, p1, p2 - p1 + 4);
        *bufp = *bufp + (p2 - buf + 4);
        *flag = 0;
        return 0;
    } else {
        req = req + (*requestp);
        *requestp = *requestp + (buf + 4096 - p1);
        if (*requestp > 65535) {
            *requestp = 0;
            return -1;
        }
        memcpy(req, p1, buf + 4096 - p1);
        *bufp = 0;
        *flag = 1;
        return 1;
    }
}

int findhost(char* request, char* hostname) {
    char *p1, *p2;
    char end[] = "\r\n";
    char host[] = "Host:";
    if ((p1 = strstr(request, host)) != NULL) {
        p1 = p1 + 6;
        p2 = strstr(p1, end);
        strncpy(hostname, p1, p2 - p1);
        hostname[p2 - p1] = '\0';
        printf("Hostname ---- %s\n", hostname);
        return 1;
    } else return 0;
}

struct in_addr** calldns(char* hostname) {
    struct in_addr **addr;
    struct hostent *he;
    if ((he = gethostbyname(hostname)) != NULL) {
        addr = (struct in_addr**) he->h_addr_list;
        return addr;
    } else return NULL;
}

int writen(int fd, const void *vptr, int n) {
    int nleft;
    int nwritten;
    const char *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0) {
            if (nwritten < 0 && errno == EINTR)
                nwritten = 0;
            else
                return (-1);
        }

        nleft -= nwritten;
        ptr += nwritten;
    }
    return (n);
}

int modifyrequest(char* request, int* requestp) {
    char *p1;
    char close[] = "close";
    if ((p1 = strstr(request, "Connection:")) == NULL) return -1;
    if (strstr(p1, "keep-alive") != NULL) {
        memcpy(p1 + 12, close, 5);
        p1 = p1 + 22;
        bcopy(p1, p1 - 5, request + *requestp - p1);
        *requestp = *requestp - 5;
        return 0;
    }
    return -1;
}

int readsocket(int fd, char *buf, int len, int timeout) {
    int rc, nrecv;
    struct timeval tv;

    fd_set readfd;
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    FD_ZERO(&readfd);
    FD_SET(fd, &readfd);
    rc = select(fd + 1, &readfd, NULL, NULL, &tv);
    if (rc > 0) {
        nrecv = read(fd, buf, len);
        return nrecv;
    } else {
        return -1;
    }
    return rc;
}
