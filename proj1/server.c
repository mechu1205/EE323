#include <stdio.h> 
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

#define QUEUE_SIZE 10

int main (int argc, char* argv[]){
    // Argument Parsing
    int input_correct = 0;
    int port;
    int c;
    
    while ( (c = getopt(argc, argv, "p:")) != -1 ){
        port = atoi(optarg);
        input_correct = 1;
    }
    if (argc != 3) input_correct = 0;
    if (input_correct==0) {
        fprintf(stderr, "wrong input format\n");
        exit(0);
    }
    
    // Socket Creation
    int sockfd_listen, sockfd_conn;
    struct sockaddr_in addr_serv, addr_cli;
    int len_cli = sizeof(addr_cli);
    
    sockfd_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_listen == -1){
        fprintf(stderr, "socket creation failure: %s\n", strerror(errno));
        exit(0);
    }
    
    // Socket Binding
    bzero(&addr_serv, sizeof(addr_serv));
    addr_serv.sin_family = AF_INET; //IPv4 protocol
    addr_serv.sin_addr.s_addr = htonl(INADDR_ANY); //IP of server
    addr_serv.sin_port = htons(port);
    
    if (bind(sockfd_listen, (struct sockaddr*)&addr_serv, sizeof(addr_serv)) == -1){
        fprintf(stderr, "socket binding failure: %s\n", strerror(errno));
        exit(0);
    }
    
    int state_listen = 0;
    state_listen = listen(sockfd_listen, QUEUE_SIZE);
    
    signal(SIGCHLD, SIG_IGN);
    while(1){
        sockfd_conn = accept(sockfd_listen, (struct sockaddr*)&addr_cli, &len_cli);
        
        int pid = fork();
        if (pid == -1){
            fprintf(stderr, "fork failure: %s\n", strerror(errno));
        }
        if (pid == 0){
            // forked thread
            // do your stuff
        }
        
        close(sockfd_conn);
    }
}