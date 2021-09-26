/* proxy.c */
#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: Close\r\n";
static const char *proxy_hdr = "Proxy-Connection: Close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *host_key = "Host";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *user_agent_key = "User-Agent";

void doit(int connfd);
void parse_uri(int *port, char *uri, char *hostname, char *path);
void build_http_header(int port, char *http_header, char *hostname, char *path, rio_t *client_rio);
int connect_server(int port, char *hostname, char *http_header);

int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    struct sockaddr_storage clientaddr;
    socklen_t clientlen;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);

        /* Print accepted message */
        getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s).\n", hostname, port);

        /* Sequential Handle client transaction */
        doit(connfd);

        close(connfd);
    }
    return 0;
}

/* Handle client HTTP transaction */
void doit(int connfd) {
    int port;
    int serverfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    char server_http_header[MAXLINE];
    rio_t rio, server_rio;

    rio_readinitb(&rio, connfd);
    rio_readlineb(&rio, buf, MAXLINE);
    /* Read client request line */
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement this method.");
        return;
    }

    /* Parse URI to get hostname, file path, port */
    parse_uri(&port, uri, hostname, path);

    /* Build HTTP header which sends to the end server */
    build_http_header(port, server_http_header, hostname, path, &rio);

    /* Connect to end server */
    serverfd = connect_server(port, hostname, server_http_header);

    if (serverfd < 0) {
        printf("Connection failed.\n");
        return;
    }

    rio_readinitb(&server_rio, serverfd);

    /* Write the HTTP header to end server */
    rio_writen(serverfd, server_http_header, strlen(server_http_header));

    /* Receive message from end server and send to the client */
    size_t n;

    while ((n = rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
        printf("Proxy received %d bytes.\n", n);
        rio_writen(connfd, buf, n);
    }
    close(serverfd);
}

/* Parse URI to get hostname, file path, port */
void parse_uri(int *port, char *uri, char *hostname, char *path) {
    *port = 80;

    char *pos1 = strstr(uri, "//");

    pos1 = pos1 != NULL ? (pos1 + 2) : uri;

    char *pos2 = strstr(pos1, ":");

    if (pos2 != NULL) {
        *pos2 = "\0";
        sscanf(pos1, "%s", hostname);
        sscanf(pos2 + 1, "%d %s", port, path);
    }

    else {
        pos2 = strstr(pos1, "/");

        if (pos2 != NULL) {
            *pos2 = "\0";
            sscanf(pos1, "%s", hostname);

            *pos2 = "/";
            sscanf(pos2, "%s", path);
        }

        else
            sscanf(pos1, "%s", hostname);
    }

    return;
}

void build_http_header(int port, char *http_header, char *hostname, char *path, rio_t *client_rio) {
    char buf[MAXLINE], request_hdr[MAXLINE], host_hdr[MAXLINE], other_hdr[MAXLINE];

    /* Print request line */
    sprintf(request_hdr, requestlint_hdr_format, path);

    /* Get other request header for client rio and change it */
    while (rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if(strcmp(buf, endof_hdr) == 0)
            break;
        
        if(!strncasecmp(buf, host_key, strlen(host_key))) {
            strcpy(host_hdr, buf);
            continue;
        }

        if (!strncasecmp(buf, connection_key, strlen(connection_key)) &&
            !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key)) &&
            !strncasecmp(buf, user_agent_key, strlen(user_agent_key)))
            strcat(other_hdr, buf);
    }

    if (strlen(host_hdr) == 0) {
        sprintf(host_hdr, host_hdr_format, hostname);
    }

    sprintf(http_header, "%s %s %s %s %s %s %s", request_hdr, host_hdr,
            conn_hdr, proxy_hdr, user_agent_hdr, other_hdr, endof_hdr);
    
    return;
}

/* Connect to end server */
inline int connect_server(int port, char *hostname, char *http_header) {
    char portstr[100];

    sprintf(portstr, "%d", port);
    return open_clientfd(hostname, portstr);
}