/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

#include "csapp.h"
#include "http_parser.h"

#include <assert.h>
#include <ctype.h>
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


void errorhandling(parser_t *parser, int connfd, char *host, char *port, char *url){
    char *http;
    char *path;
    char *scheme;
    char *method;
    if ((parser_retrieve(parser, HOST, &host) < 0)
        || (parser_retrieve(parser, PORT, &port) < 0)
        || (parser_retrieve(parser, HTTP_VERSION, &http) < 0)
        || (parser_retrieve(parser, PATH, &path) < 0)
        || (parser_retrieve(parser, URI, &url) < 0)
        || (parser_retrieve(parser, SCHEME, &scheme) < 0)
        || (parser_retrieve(parser, METHOD, &method) < 0)){
        perror("bad request");
    }
    if (strcmp(method, "GET") != 0) {
        perror("not implemented");
        return;
    } else {
        parser_retrieve(parser, PATH, &path);
        parser_retrieve(parser, HTTP_VERSION, &http);
        parser_retrieve(parser, METHOD, &method);
    }
}

// handles HTTP request/response transaction
void request(int connfd) {
    char *host;
    char *port;
    char *url;

    char buf[MAXLINE], body[MAXLINE];
    rio_t rioClient, rioServer;
    int serverfd;
    ssize_t size;
    parser_t *parser;
    parser_state response;

    rio_readinitb(&rioClient, connfd);
    //rio_readlineb(&rioClient, buf, MAXLINE);
    
    if (rio_readlineb(&rioClient, buf, sizeof(buf)) <= 0){
        return;
    }
    parser = parser_new();
    response = parser_parse_line(parser, buf);
    
    errorhandling(parser, connfd, host, port, url);
    
    serverfd = open_clientfd(host, port);
    if (serverfd < 0) {
        return;
    }

    rio_readinitb(&rioServer, serverfd);
    size = rio_readnb(&rioServer, body, MAXLINE);
    while (size != 0) {
        if (strcasecmp(buf, "\r\n")==0) {
            rio_writen(connfd, "\r\n", strlen("\r\n"));
            break;
        }else{
            rio_writen(connfd, body, size);
            size = rio_readnb(&rioServer, body, MAXLINE);
        }
    }
    close(serverfd);
    parser_free(parser);
}


void *thread(void *vargp) {
    int connfd = *((int*)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    request(connfd);
    close(connfd);
    return NULL;
}


int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    signal(SIGPIPE, SIG_IGN);
    listenfd = open_listenfd(argv[1]);

    while (1) {
        pthread_t bot;
        connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &clientlen);
        int *connfdp = malloc(sizeof(int));
        *connfdp = connfd;
        clientlen = sizeof(clientaddr);
        pthread_create(&bot, NULL, thread, connfdp);
    }
}

