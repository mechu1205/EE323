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

void parse_url(char *url, char **host, char **path, int *port){
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
    int idx_port = ptcl_found? (idx_schm + strlen(scheme_mark)) : 0;
    int port_found = 0;
    char *ptr_port = strchar(url, ':');
    if (ptr_port != NULL){
        port_found = 1;
        ptr_port ++;
        port_no = atoi(ptr_port);
    }
    
    // Find path by searching the first '/' character in URL (after protocol mark)
    int path_found = 0;
    char *ptr_path = strchar( (ptcl_found ? (ptr_schm + strlen(scheme_mark)) : url), '/'); 
    if (ptr_path != NULL){
        *path = (char *)malloc(len_url + url - ptr_path + 1);
        strcpy(*path, ptr_path);
    }
    else{
        *path = (char *)malloc(2);
        strcpy(*path, "/");
    }
    
    // Find host by grabbing everything in between URL start or protocol mark
    // And URL end or port or path
    int host_found = 0;
    char *ptr_host = strchar( (ptcl_found ? (ptr_schm + strlen(scheme_mark)) : url), '/'); 
    if (ptr_host != NULL){
        *path = (char *)malloc(len_url + url - ptr_path + 1);
        strcpy(*path, ptr_path);
    }
    else{
        *path = (char *)malloc(2);
        strcpy(*path, "/");
    }
    
    int idx_host_start = ptcl_found? (idx_schm + strlen(scheme_mark)) : 0;
    int idx_host_end = port_found? idx_port : (path_found? idx_path : len_url); //index right after host
    *host = (char *)malloc(idx_host_end - idx_host_start + 1);
    memcpy(*host, url + idx_host_start, idx_host_end - idx_host_start);
    (*host)[idx_host_end - idx_host_start] = '\0';
    
    *port = port_no;
    
    // printf("%s, %d, %s\n", *host, *port, *path);fflush(stdout);
}