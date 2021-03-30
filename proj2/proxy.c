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
#include <sys/param.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#define BUFFER_SIZE_LARGE (1000*1000*1000) //1G
#define BUFFER_SIZE_MEDIUM (1000*1000) //1M
#define BUFFER_SIZE_SMALL (1000) //1k

#define QUEUE_SIZE 10
#define BLACKLIST_REDIRECTION ("www.warning.or.kr")

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
        exit(1);
    }
    int port_proxy = atoi(argv[1]);
    
    // Create and Bind Socket
    
    int sockfd_listen, sockfd_conn;
    struct sockaddr_in addr_proxy, addr_cli;
    int len_cli = sizeof(addr_cli);
    
    sockfd_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_listen < 0){
        fprintf(stderr, "socket creation failure: %s\n", strerror(errno));
        exit(1);
    }
    
    bzero(&addr_proxy, sizeof(addr_proxy));
    addr_proxy.sin_family = AF_INET; //IPv4 protocol
    addr_proxy.sin_addr.s_addr = htonl(INADDR_ANY); //IP of server
    addr_proxy.sin_port = htons(port_proxy);
    
    if (bind(sockfd_listen, (struct sockaddr*)&addr_proxy, sizeof(addr_proxy)) < 0){
        fprintf(stderr, "socket binding failure: %s\n", strerror(errno));
        exit(1);
    }
    
    int state_listen = 0;
    state_listen = listen(sockfd_listen, QUEUE_SIZE);
    
    signal(SIGCHLD, SIG_IGN);
    while(1){
        char *msg = malloc(BUFFER_SIZE_MEDIUM);
        char *buffer_recv = malloc(BUFFER_SIZE_MEDIUM);
        sockfd_conn = accept(sockfd_listen, (struct sockaddr*)&addr_cli, &len_cli);
        // fprintf(stdout, "connection accepted, forking process..\n"); fflush(stdout); // debug line
        int pid = fork();
        if (pid == -1){
            fprintf(stderr, "fork failure: %s\n", strerror(errno));
        }
        if (pid == 0){
            // Receive Message from Client
            bzero((void *)msg, BUFFER_SIZE_MEDIUM);
            
            int len_recv = 0;
            int len_recv_tot = 0;
            char *eoh = "\r\n\r\n";
            char *ptr_eoh;
            do {
                // Receive and copy incoming message to msg
                // stop looping when end of header is reached
                len_recv = recv(sockfd_conn, buffer_recv, BUFFER_SIZE_MEDIUM, 0);
                if (len_recv < 0){
                    fprintf(stderr, "reception failure: %s\n", strerror(errno));
                    exit(1);
                }
                memcpy(msg + len_recv_tot, buffer_recv, MIN(len_recv, BUFFER_SIZE_MEDIUM - len_recv_tot));
                len_recv_tot += MIN(len_recv, BUFFER_SIZE_MEDIUM - len_recv_tot);
                // fprintf(stdout, "received %d B (total %d B)\n", len_recv, len_recv_tot); fflush(stdout);
                
                ptr_eoh = strstr(msg, eoh);
            }while ((ptr_eoh == NULL) && len_recv_tot < BUFFER_SIZE_MEDIUM);
            // Delete everything after header
            bzero(ptr_eoh + strlen(eoh), BUFFER_SIZE_MEDIUM - (ptr_eoh - msg + strlen(eoh)));
            // fprintf(stdout, "<MESSAGE FROM CLIENT>\n%s</MESSAGE FROM CLIENT>\n", msg); fflush(stdout); // debug
            
            // Parse Message
            // Parser for "GET"
            char *ptr;
            char *get = "GET";
            if ((memcmp(msg, get, strlen(get)) != 0) || !isspace(msg[strlen(get)]) ){
                if (send400(sockfd_conn, 0)<0) exit(1);
                exit(0);
            }
            
            // fprintf(stdout, "GET request found\n"); fflush(stdout); // debug
            
            // Parse URL in request line
            int idx = strlen(get);
            for(; !isspace(msg[idx]); idx++) {} // skip to next whitespace
            for(; isspace(msg[idx]); idx++) {} // skip whitespace
            int idx_aux = idx;
            for(; !isspace(msg[idx]); idx++) {} // skip to next whitespace
            char *url_req = malloc(idx - idx_aux + 1);
            bzero(url_req, idx - idx_aux + 1);
            memcpy(url_req, msg + idx_aux, idx - idx_aux);
            
            int port_req;
            char *host_req = NULL;
            char *path_req = NULL;
            parse_url(url_req, &host_req, &path_req, &port_req);
            
            // fprintf(stdout, "URL in request line parsed: [%s]:[%d][%s]\n", host_req, port_req, path_req); fflush(stdout); // debug
            
            // Parse HTTP version
            // accept only 1.0 (although according to protocol should accept <=1.0)
            char *http_ver = "HTTP/1.0";
            for(; !isspace(msg[idx]); idx++) {} // skip to next whitespace
            for(; isspace(msg[idx]); idx++) {} // skip whitespace
            idx_aux = idx;
            for(; !isspace(msg[idx]); idx++) {} // skip to next whitespace
            if (memcmp(msg + idx_aux, http_ver, strlen(http_ver)) != 0){
                if (send400(sockfd_conn, 0)<0) exit(1);
                exit(0);
            }
            
            // fprintf(stdout, "HTTP version matched\n"); fflush(stdout); // debug
            
            // Parse for "Host" Header
            // "Host" header should exist uniquely (i.e. no duplicates)
            char *host_header = "\r\nHost:";
            ptr = strstr(msg, host_header);
            if (ptr == NULL){
                if (send400(sockfd_conn, 0)<0) exit(1);
                exit(0);
            }
            idx = ptr - msg + strlen(host_header);
            for(; isspace(msg[idx]); idx++) {} // skip whitespace
            idx_aux = idx;
            for(; !isspace(msg[idx]); idx++) {} // skip to next whitespace
            char *url_hd = malloc(idx - idx_aux + 1);
            bzero(url_hd, idx - idx_aux + 1);
            memcpy(url_hd, msg + idx_aux, idx - idx_aux);
            ptr = strstr(msg + idx, host_header);
            if (ptr != NULL){
                if (send400(sockfd_conn, 0)<0) exit(1);
                exit(0);
            }
            
            // fprintf(stdout, "Host header uniquely found\n"); fflush(stdout); // debug
            
            // Parse URL found in Host header
            // format should be <Host>:<Port> where <Host> matches host in request line
            int port_hd;
            char *host_hd = NULL;
            char *path_hd = NULL;
            if ((parse_url(url_hd, &host_hd, &path_hd, &port_hd) & 5) || (strlen(host_req) && (strcmp(host_req, host_hd) != 0)) ){
                // fprintf(stdout, "URL in header is in wrong format: %s", url_hd); fflush(stdout); // debug
                if (send503(sockfd_conn, 0)<0) exit(1);
                exit(0);
            }
            if (!strlen(host_req)) {
                free(host_req);
                host_req = host_hd;
            }
            
            // fprintf(stdout, "Message parsed; no flaws were found.\n"); fflush(stdout); // debug
            
            // Search through stdin redirection for host_req
            char *buffer_in = malloc(BUFFER_SIZE_SMALL);
            int port_in; char *host_in; char *path_in;
            bzero(buffer_in, BUFFER_SIZE_SMALL);
            int is_blacklisted = 0;
            while((fgets(buffer_in, BUFFER_SIZE_SMALL , stdin) != NULL) && !is_blacklisted){
                // trim whitespace at the end of buffer_in
                while( (strlen(buffer_in) > 0) && isspace(buffer_in[strlen(buffer_in)-1]) ){
                    buffer_in [strlen(buffer_in) - 1] = '\0';
                } 
                // fprintf(stdout, "blacklist entry: %s\n", buffer_in);
                parse_url(buffer_in, &host_in, &path_in, &port_in);
                // fprintf(stdout, "blacklisted host: %s\n", host_in);
                // fprintf(stdout, "strcmp(\"%s\", \"%s\") = %d\n", host_req, host_in, strcmp(host_req, host_in)); fflush(stdout);
                if (strcmp(host_req, host_in) == 0){
                    // blacklisted host; redirect to BLACKLIST_REDIRECTION
                    // fprintf(stdout, "requested host is blocked\n"); fflush(stdout);
                    host_req = BLACKLIST_REDIRECTION;
                    port_req = port_in;
                    path_req = path_in;
                    is_blacklisted = 1; // used to break out safely from loop
                }
                bzero(buffer_in, BUFFER_SIZE_SMALL);
            }
            
            // Make Connection with Host
            
            int sockfd_host;
            struct addrinfo *addr_host, hints, *res;
            
            bzero(&hints, sizeof(hints));
            hints.ai_family = PF_INET;
            hints.ai_socktype = SOCK_STREAM;
            // hints.ai_flags |= AI_CANONNAME;
            
            int gotaddr = getaddrinfo(host_req, NULL, &hints, &res);
            if (gotaddr  != 0) {
                fprintf(stderr, "getaddrinfo failure: %d\n",gotaddr);
                exit(1);
            }
                        
            // Socket Creation
            sockfd_host = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd_host < 0){
                fprintf(stderr, "socket creation failure: %s\n", strerror(errno));
                exit(1);
            }
            
            // Socket Connection
            int connected = 0;
            while (res && !connected){
                addr_host = res;
                ((struct sockaddr_in *)(addr_host->ai_addr))->sin_port = htons(port_req);
                
                if (connect(sockfd_host, (struct sockaddr *)addr_host->ai_addr, sizeof(struct sockaddr_in)) < 0){
                    res = res->ai_next;
                }else{
                    connected = 1;
                }
            }
            
            if (!connected){
                fprintf(stderr, "failed to establish connection to host\n");
                if (send503(sockfd_conn, 0)<0) exit(1);
                exit(0);
            }
            // fprintf(stdout, "Established connection to host\n"); fflush(stdout);
            
            // Format and send GET Message
            // Trim all headers but "Host"
            free(msg);
            asprintf(&msg, "GET %s HTTP/1.0\r\n"
                "Host: %s:%d\r\n"
                "\r\n",
                path_req, host_req, port_req);
            if (send_all(sockfd_host, msg, strlen(msg), 0) < 0) exit(1);
            // fprintf(stdout, "<MESSAGE SENT>\n%s</MESSAGE SENT>\n", msg); fflush(stdout); // debug
            
            len_recv = 0;
            len_recv_tot = 0;
            ptr_eoh = NULL;
            char *length_header = "\r\nContent-Length:";
            char *ptr_length;
            int content_length = 0;
            int header_length = 0;
            msg = malloc(BUFFER_SIZE_MEDIUM);
            // fprintf(stdout, "Receiving response from host..\n"); fflush(stdout);
            do {
                len_recv = recv(sockfd_host, buffer_recv, BUFFER_SIZE_MEDIUM, 0);
                if (len_recv < 0){
                    fprintf(stderr, "reception failure: %s\n", strerror(errno));
                    exit(1);
                }
                // fwrite(buffer_recv, len_recv, 1, stdout); // debug
                if (ptr_eoh == NULL){
                    memcpy(msg + len_recv_tot, buffer_recv, MIN(len_recv, BUFFER_SIZE_MEDIUM - len_recv_tot));
                    len_recv_tot += MIN(len_recv, BUFFER_SIZE_MEDIUM - len_recv_tot);
                    // fprintf(stdout, "received %d B (total %d B)\n", len_recv, len_recv_tot); fflush(stdout);
                    ptr_eoh = strstr(msg, eoh);
                    
                    if (ptr_eoh != NULL){
                        if (send_all(sockfd_conn, msg, len_recv_tot, 0) < 0) exit(1);
                        header_length = ptr_eoh - msg + strlen(eoh);
                        
                        ptr_length = strstr(msg, length_header);
                        if (ptr_length != NULL){
                            ptr_length += strlen(length_header);
                            for(; isspace(*ptr_length); ptr_length++) {} // skip whitespace
                            content_length = atoi(ptr_length);
                        }
                    }   
                }else{
                    len_recv_tot += len_recv;
                    // fprintf(stdout, "received %d B (total %d B)\n", len_recv, len_recv_tot); fflush(stdout);
                    if (send_all(sockfd_conn, buffer_recv, len_recv, 0) < 0) exit(1);
                }
                
            }while ((ptr_eoh == NULL) | (len_recv_tot < header_length + content_length));
            
            // fprintf(stdout, "Message reception/relay finished\n"); fflush(stdout); // debug
            
            free(msg);
            free(buffer_recv);
        }
        // Close connection
        close (sockfd_conn);
    }
    
    return 0; 
}