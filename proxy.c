/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

#include "csapp.h"

#include <assert.h>
#include <ctype.h>
#include <http_parser.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1";

void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
                       "<!DOCTYPE html>\r\n"
                       "<html>\r\n"
                       "<head><title>Proxy Error</title></head>\r\n"
                       "<body bgcolor=\"ffffff\">\r\n"
                       "<h1>%s: %s</h1>\r\n"
                       "<p>%s</p>\r\n"
                       "<hr /><em>The Proxy server</em>\r\n"
                       "</body></html>\r\n",
                       errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
                      "HTTP/1.0 %s %s\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

void write_headers(int serverfd, const char *path, const char *host,
                   const char *port) {
    char hostH[MAXLINE] = "\0";
    char userH[MAXLINE] = "\0";
    char request[MAXLINE] = "\0";
    char cnctH[MAXLINE] = "\0";
    char proxcnctH[MAXLINE] = "\0";

    // request header
    strcpy(request, "GET ");
    strcat(request, path);
    strcat(request, " HTTP/1.0\r\n");
    rio_writen(serverfd, request, strlen(request));

    // host header
    strcpy(hostH, "Host: ");
    strcat(hostH, host);
    strcat(hostH, ":");
    strcat(hostH, port);
    strcat(hostH, "\r\n");
    rio_writen(serverfd, hostH, strlen(hostH));

    // user agent header
    strcat(userH, "User-Agent: ");
    strcat(userH, header_user_agent);
    strcat(userH, "\r\n");
    rio_writen(serverfd, userH, strlen(userH));

    // connection header
    strcpy(cnctH, "Connection: close\r\n");
    rio_writen(serverfd, cnctH, strlen(cnctH));
    strcpy(proxcnctH, "Proxy-Connection: close\r\n");
    rio_writen(serverfd, proxcnctH, strlen(proxcnctH));
}

// handles HTTP request/response transaction
void request(int connfd) {
    char *host;
    char *port;
    char *http;
    char *path;
    char *uri;
    char *method;
    char *scheme;

    char buf[MAXLINE], body[MAXLINE];
    rio_t rioClient, rioServer;
    int serverfd;
    ssize_t size;
    parser_t *parser;
    parser_state response;

    rio_readinitb(&rioClient, connfd);
    // rio_readlineb(&rioClient, buf, MAXLINE);

    if (rio_readlineb(&rioClient, buf, sizeof(buf)) <= 0)
        return;

    parser = parser_new();

    response = parser_parse_line(parser, buf);

    // error handling
    if ((parser_retrieve(parser, HOST, &host) < 0) ||
        (parser_retrieve(parser, PORT, &port) < 0) ||
        (parser_retrieve(parser, HTTP_VERSION, &http) < 0) ||
        (parser_retrieve(parser, PATH, &path) < 0) ||
        (parser_retrieve(parser, URI, &uri) < 0) ||
        (parser_retrieve(parser, SCHEME, &scheme) < 0) ||
        (parser_retrieve(parser, METHOD, &method) < 0))
        clienterror(connfd, "400", "Bad Request",
                    "Proxy received a bad request");
    if (strcmp(method, "GET") != 0) {
        clienterror(connfd, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    } else {
        parser_retrieve(parser, PATH, &path);
        parser_retrieve(parser, HTTP_VERSION, &http);
        parser_retrieve(parser, METHOD, &method);
    }

    serverfd = open_clientfd(host, port);
    if (serverfd >= 0) {
        write_headers(serverfd, path, host, port);
        while ((size = rio_readlineb(&rioClient, buf, MAXLINE)) != 0) {
            if (strncmp("Host:", buf, 5) == 0 ||
                strncmp("User-Agent:", buf, 11) == 0 ||
                strncmp("Connection:", buf, 11) == 0 ||
                strncmp("Proxy-Connection:", buf, 16) == 0)
                continue;
            else
                rio_writen(serverfd, buf, size);
            if (strcmp(buf, "\r\n") == 0) {
                rio_writen(serverfd, "\r\n", strlen("\r\n"));
                break;
            }
        }

        rio_readinitb(&rioServer, serverfd);
        size = rio_readnb(&rioServer, body, MAXLINE);
        while (size != 0) {
            if (strcmp(body, "\r\n") == 0) {
                rio_writen(connfd, "\r\n", strlen("\r\n"));
                break;
            }
            rio_writen(connfd, body, size);
            size = rio_readnb(&rioServer, body, MAXLINE);
        }
    } else
        clienterror(connfd, "400", "Bad Request", "open_clientfd failed");
    close(serverfd);
    parser_free(parser);
}

int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    listenfd = open_listenfd(argv[1]);
    signal(SIGPIPE, SIG_IGN);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        request(connfd);
        close(connfd);
    }
}
