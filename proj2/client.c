#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#define BUFFER_SIZE_LARGE (1000*1000*1000) //1G
#define BUFFER_SIZE_MEDIUM (1000*1000) //1M
#define BUFFER_SIZE_SMALL (1000) //1k

#define PORT 8888

void error(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

int parse_url(char *url, char **host, char **path, int *port){
    // Parses string URL and saves the result to HOST, PATH, and PORT
    // e.g. URL = "www.example.com:80/index.html"
    // *host = "www.example.com", *path = "/index.html", *port = 80
    // return ptcl_found*4 + port_found*2 + path_found
    
    int port_no = 80; // default port number to be returned
    int len_url = strlen(url);
    
    char *scheme_mark = "://";
    int idx_schm = 0;
    int ptcl_found = 0;
    char *ptr_schm = strstr(url, scheme_mark);
    if (ptr_schm != NULL){
        ptcl_found = 1;
        idx_schm = ptr_schm - url;
    }
    
    // Find port by searching the first ':' character in URL (after protocol mark)
    int port_found = 0;
    char *ptr_port = strchr(url + (ptcl_found? (idx_schm + strlen(scheme_mark)) : 0), ':');
    if (ptr_port != NULL){
        port_found = 1;
        port_no = atoi(ptr_port + 1);
    }
    
    // Find path by searching the first '/' character in URL (after protocol mark)
    int path_found = 0;
    char *ptr_path = strchr( (ptcl_found ? (ptr_schm + strlen(scheme_mark)) : url), '/'); 
    if (ptr_path != NULL){
        path_found = 1;
        *path = (char *)malloc(len_url + url - ptr_path + 1);
        strcpy(*path, ptr_path);
    }
    else{
        *path = (char *)malloc(2);
        strcpy(*path, "/");
    }
    
    // Find host by grabbing everything in between URL start or protocol mark
    // And URL end or port or path
    
    int idx_host_start = ptcl_found? (idx_schm + strlen(scheme_mark)) : 0;
    int idx_host_end = port_found? (ptr_port - url) : (path_found? (ptr_path - url) : len_url); //index right after host
    *host = (char *)malloc(idx_host_end - idx_host_start + 1);
    memcpy(*host, url + idx_host_start, idx_host_end - idx_host_start);
    (*host)[idx_host_end - idx_host_start] = '\0';
    
    *port = port_no;
    
    return ptcl_found * 4 + port_found * 2 + path_found;
    // printf("%d, %d, %d\n", ptcl_found, port_found, path_found);
    // printf("%s, %d, %s\n", *host, *port, *path);fflush(stdout);
}

int main (int argc, char* argv[]){
    // Argument Parsing
    if (argc < 3 || (strcmp (argv[1], "-G") != 0 && strcmp (argv[1], "-P") != 0)){
        printf ("Usage:\n        %s -P <URL>        HTTP 1.0 POST from stdin\n"
              "        %s -G <URL>        HTTP 1.0 GET to stdin\n", argv[0], argv[0]);
        exit (EXIT_FAILURE);
    }
    char *url = argv[2];
    
    int mode = -1;
    if(strcmp (argv[1], "-G") == 0 ){
        mode = 1;
    }else if (strcmp (argv[1], "-P") == 0 ){
        mode = 0;
    }
    
    int port;
    char *host = NULL;
    char *path = NULL;
    parse_url(url, &host, &path, &port);
    // printf("%s -> %s, %d, %s\n", url, host, port, path);
    
    char *msg;
    char *buffer_in;
    char *buffer_send;
    char *buffer_recv;
    int sockfd;
    struct addrinfo *addr_host, hints, *res, *res_iter;
    
    bzero(&hints, sizeof(hints));
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;
    // hints.ai_flags |= AI_CANONNAME;
    
    int gotaddr = getaddrinfo(host, NULL, &hints, &res);
    if (gotaddr  != 0) {
        fprintf(stderr, "getaddrinfo failure: %d\n",gotaddr);
        exit(EXIT_FAILURE);
    }
    
    signal(SIGPIPE, SIG_IGN);
    
    // Socket Creation
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0){
        fprintf(stderr, "socket creation failure: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // Socket Connection
    res_iter = res;
    int connected = 0;
    while (res_iter && !connected){
        addr_host = res_iter;
        ((struct sockaddr_in *)(addr_host->ai_addr))->sin_port = htons(port);
        
        if (connect(sockfd, (struct sockaddr *)addr_host->ai_addr, sizeof(struct sockaddr_in)) < 0){
            res_iter = res_iter->ai_next;
        }else{
            connected = 1;
        }
    }
    
    if (mode == 1){ // GET
        // Message Formatting
        // asprintf(&msg, "GET http://www.naver.com/ HTTP/1.0\r\nHost: www.naver.com\r\n\r\n");
        asprintf(&msg, "GET http://www.daum.net/ HTTP/1.0\r\nHost: www.daum.net\r\n\r\n");
        int msg_len = strlen(msg);
        
        // Send Message
        int len_sent = 0; int temp = 0;
        do{
            temp = send (sockfd, msg, msg_len, 0);
            if(temp<0){
                fprintf(stderr, "failed to send message: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            len_sent += temp;
        }while (len_sent < msg_len);
        
        // Receive Message and print Contents to stdout
        int len_recv = 0;
        buffer_recv = malloc(BUFFER_SIZE_MEDIUM);
        bzero(buffer_recv, BUFFER_SIZE_MEDIUM);
        
        char* eoh = "\r\n\r\n"; // end of header
        int i = 0;
        int eoh_reached = 0;
        
        do {
            len_recv = recv(sockfd, buffer_recv + 3, BUFFER_SIZE_MEDIUM - 3, 0);
            if (len_recv < 0){
                fprintf(stderr, "reception failure: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            fwrite(buffer_recv + 3, len_recv, 1, stdout);
            // for (i = 0; (i <= len_recv-4) && !eoh_reached ; i++){
            //     if (memcmp(buffer_recv + i, eoh, 4) == 0) {
            //         eoh_reached = 1;
            //     }
            // }
            
            // if (eoh_reached)
            //     fwrite(buffer_recv + 3 + i, len_recv - i, 1, stdout);
            // else
            //     memcpy(buffer_recv, buffer_recv + len_recv, 3);
        }while (len_recv > 0);
        
        free(msg);
        close(sockfd);
    }
    
    if (mode == 0){ // POST
        int read_size;
        int content_length = 0;
        buffer_in = malloc(BUFFER_SIZE_SMALL);
        buffer_send = malloc(BUFFER_SIZE_LARGE);
        while(read_size = read(STDIN_FILENO, buffer_in, BUFFER_SIZE_SMALL)){
            memcpy(buffer_send + content_length, buffer_in, read_size);
            content_length += read_size;
        }
        
        asprintf(&msg, "POST /test.html HTTP/1.0\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: %d\r\n"
            "\r\n",
            content_length);
        int msg_len = strlen(msg);
        // fprintf(stdout, "%s", msg); fflush(stdout); // debug
        
        int len_sent = 0; int temp = 0;
        do{
            temp = send (sockfd, msg, strlen(msg), 0);
            if(temp<0){
                fprintf(stderr, "failed to send message: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            len_sent += temp;
        }while (len_sent < strlen(msg));
        
        len_sent = 0; temp = 0;
        do{
            temp = send (sockfd, buffer_send, content_length, 0);
            if(temp<0){
                fprintf(stderr, "failed to send message: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            len_sent += temp;
        }while (len_sent < content_length);
        
        free(msg);
        close(sockfd);
    }
    
    free(host);
    free(path);
    return 0;
}