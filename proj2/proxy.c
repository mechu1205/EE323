#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h> 
#include <sys/types.h> 
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#define BUFFER_SIZE_LARGE (1000*1000*1000) //1G
#define BUFFER_SIZE_MEDIUM (1000*1000) //1M
#define BUFFER_SIZE_SMALL (1000) //1k

#define QUEUE_SIZE 10

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

int send_all (int sockfd, char *msg, size_t len_msg, int flags){
    int len_sent = 0;
    int temp = 0;
    do{
        temp = send (sockfd, msg, len_msg, flags);
        if(temp<0){
            fprintf(stderr, "failed to send message: %s\n", strerror(errno));
            return -1;
        }
        len_sent += temp;
    }while (len_sent < len_msg);
    return 0;
}

int send400 (int sockfd, int flags){
    char *msg = "HTTP/1.0 400 Bad Request\r\n\r\n";
    return send_all(sockfd, msg, strlen(msg), 0);
}

int send503 (int sockfd, int flags){
    char *msg = "HTTP/1.0 503 Service Unavailable\r\n\r\n";
    return send_all(sockfd, msg, strlen(msg), 0);
}

int main (int argc, char* argv[]){
    // Parse arguments
    if ( (argc != 2) || !atoi(argv[1]) ){
        fprintf (stdout, "Usage: %s <PORT>\n", argv[0]);
        return 1;
    }
    int port_proxy = atoi(argv[1]);
    
    // Create and Bind Socket
    
    int sockfd_listen, sockfd_conn;
    struct sockaddr_in addr_proxy, addr_cli;
    int len_cli = sizeof(addr_cli);
    
    sockfd_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_listen < 0){
        fprintf(stderr, "socket creation failure: %s\n", strerror(errno));
        return 1;
    }
    
    bzero(&addr_proxy, sizeof(addr_proxy));
    addr_proxy.sin_family = AF_INET; //IPv4 protocol
    addr_proxy.sin_addr.s_addr = htonl(INADDR_ANY); //IP of server
    addr_proxy.sin_port = htons(port_proxy);
    
    if (bind(sockfd_listen, (struct sockaddr*)&addr_proxy, sizeof(addr_proxy)) < 0){
        fprintf(stderr, "socket binding failure: %s\n", strerror(errno));
        return 1;
    }
    
    return 0; 
}