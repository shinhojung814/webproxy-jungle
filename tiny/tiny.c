/* tiny.c - A simple, iterative HTTP/1.0 Web server
that uses the GET method to serve static and dynamic content */

#include "csapp.h"

void doit(int fd);
void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_requesthdrs(rio_t *rp);

int parse_uri(char *uri, char *filename, char *cgiargs);

void get_filetype(char *filename, char *filetype);
void serve_static(int fd, char *filename, int filesize);
void serve_dynamic(int fd, char *filename, char *cgiargs);

int main(int argc, char **argv) {
    struct sockaddr_storage clientaddr;
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;

    /* Check command-line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port> \n", argv[0]);
        exit(0);
    }
    
    listenfd = open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);

        getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);

        printf("Accepted connection from (%s, %s) \n", hostname, port);

        doit(connfd);
        close(connfd);
    }
}

void doit(int fd) {
    struct stat sbuf;
    int is_static;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    rio_readinitb(&rio, fd);
    rio_readlineb(&rio, buf, MAXLINE);

    printf("Request headers: \n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        client_error(fd, method, "501", "Not implemented", "TINY does not implement this method");
        return;
    }
    read_requesthdrs(&rio);

    /* Parse URI from GET requests */
    is_static = parse_uri(uri, filename, cgiargs);
    
    if (stat(filename, &sbuf) < 0) {
        client_error(fd, filename, "404", "Not found", "TINY couldn't find this file");
        return;
    }

    /* Serve static content */
    if (is_static) {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            client_error(fd, filename, "403", "Forbidden", "TINY couldn't read the file");
            return;
        }
        serve_static(fd, filename, sbuf.st_size);
    }

    /* Serve dynamic content */
    else {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            client_error(fd, filename, "403", "Forbidden", "TINY couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs);
    }
}

void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>TINY Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff""\r\n", body);
    sprintf(body, "%s%s: s\r\n", body, longmsg, cause);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The TINY Web Server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    rio_readlineb(rp, buf, MAXLINE);

    while(strcmp(buf, "\r\n")) {
        rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;

    /* Static content */
    if (!strstr(uri, "cgi-bin")) {
        strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);

        if (uri[strlen(uri) - 1] == '/')
            strcat(filename, "home.html");

        return 1;
    }

    /* Dynamic content */
    else {
        ptr = index(uri, '?');

        if (ptr) {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }

        else
            strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);

        return 0;
    }
}

/* get_filetype - Derive file type from filename */

void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    
    else if (strstr(filename, "jpg"))
        strcpy(filetype, "image/jpeg");
    
    else
        strcpy(filetype, "text/plain");
}

void serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    /* Send response headers to client */
    get_filetype(filename, filetype);

    sprintf(buf, "HTTP/1.0 200 OK\n");
    sprintf(buf, "%sServer: TINY Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

    rio_writen(fd, buf, strlen(buf));

    printf("Response headers: \n");
    printf("%s", buf);

    /* Send response body to client */
    srcfd = open(filename, O_RDONLY, 0);
    srcp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    
    close(srcfd);

    rio_writen(fd, srcp, filesize);
    munmap(srcp, filesize);
}

void serve_dynamic(int fd, char *filename, char *cgiargs) {
    char buf[MAXLINE], *emptylist[] = {NULL};

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: TINY Web Server\r\n");
    rio_writen(fd, buf, strlen(buf));

    /* Child */
    if (fork() == 0) {
        /* Real server would set all CGI vars here */
        setenv("QUERY_STRING", cgiargs, 1);
        /* Redirect stdout to client */
        dup2(fd, STDOUT_FILENO);
        /* Run CGI program */
        execve(filename, emptylist, environ);
    }
    /* Parent waits for and reaps child */
    wait(NULL);
}
