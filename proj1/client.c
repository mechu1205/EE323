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

#define MSG_SIZE 1000000 //10M = 10000000
#define HEADER_SIZE 8
#define BUFFER_SIZE (MSG_SIZE-HEADER_SIZE)

#define PORT 8888

int main (int argc, char* argv[]){
    // argument parser, use getopt
    // -o: op, -s: shift, -p: port, -h: host
    
    // Argument Parsing
    int input_correct = 0;
    struct sockaddr_in addr_host;
    char *host;
    uint16_t port;
    uint8_t op, shift;
    int c = 0;
    
    while ( (c = getopt(argc, argv, "h:p:o:s:")) != -1 ){
        switch (c){
            case 'h': 
                // inet_pton(AF_INET, optarg, &addr_host.sin_addr);
                host = malloc(strlen(optarg));
                strcpy(host, optarg);
                input_correct ++;
                break;
            case 'p':
                port = htons(atoi(optarg));
                input_correct ++;
                break;
            case 'o':
                op = (uint8_t)(htons(atoi(optarg))>>8);
                input_correct ++;
                break;
            case 's':
                shift = (uint8_t)(htons(atoi(optarg))>>8);
                input_correct ++;
                break;
            default:
                break;
        }
    }
    if (argc != 9) input_correct = 0;
    if (input_correct != 4) {
        fprintf(stderr, "wrong input format\n");
        exit(0);
    }
    
    printf("host, port, op, shift: %s, %d, %d, %d\n", host,port,op,shift); fflush(stdout); // debug line
    
    char *buffer_in = malloc(BUFFER_SIZE);
    char *buffers_out[1];
    // char msg[MSG_SIZE];
    char *msg;
    int read_size = 0;
    uint32_t len = 0;
    int index_msg = 0;
    
    // Loop until EOF is reached
    while(read_size = read(STDIN_FILENO, buffer_in, BUFFER_SIZE)){
        // fprintf(stdout, "%d Bytes read\n",read_size); fflush(stdout); // debug line
        len = htonl(read_size + 8);
        msg = malloc(len);
        memcpy(msg, &op, sizeof(op));
        // fprintf(stdout, "op: %d (should be %d)\n", *((uint8_t*)msg), op); fflush(stdout); // debug line
        memcpy(msg+1, &shift, sizeof(shift));
        // fprintf(stdout, "shift: %d (should be %d)\n", *((uint8_t*)(msg+1)), shift); fflush(stdout); // debug line
        memcpy(msg+4, &len, 4);
        // fprintf(stdout, "len: %d (should be %d)\n", *((uint32_t*)(msg+4)), len); fflush(stdout); // debug line
        memcpy(msg+8, buffer_in, read_size);
        // fprintf(stdout, "memcpy finished\n"); fflush(stdout); // debug line
        
        // TODO: checksum
        
        // Socket Creation
        int sockfd;
        
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1){
            fprintf(stderr, "socket creation failure: %s\n", strerror(errno));
            exit(0);
        }
        
        addr_host.sin_family = AF_INET; //IPv4 protocol
        inet_pton(AF_INET, host, &addr_host.sin_addr);
        addr_host.sin_port = port;
        
        fprintf(stdout, "attempting connection..\n"); fflush(stdout); // debug line
        if (connect(sockfd, (struct sockaddr *)&addr_host, sizeof(addr_host)) < 0){
            fprintf(stderr, "socket connection failure: %s\n", strerror(errno));
            exit(0);
        }else{
            // Connection Established
            send (sockfd, msg, len, 0);
        }
        
        
    }
}