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
    struct sockaddr_in host;
    uint16_t port;
    uint8_t op, shift;
    int c = 0;
    
    while ( (c = getopt(argc, argv, "h:p:o:s:")) != -1 ){
        switch (c){
            case 'h': 
                inet_pton(AF_INET, optarg, &host);
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
    
    printf("port, op, shift: %d, %d, %d\n", port,op,shift);
    fflush(stdout);
    
    char *buffer_in = malloc(BUFFER_SIZE);
    char *buffers_out[1];
    char msg[MSG_SIZE+1];
    int read_size = 0;
    uint32_t len = 0;
    int index_msg = 0;
    
    // Loop until EOF is reached
    while(read_size = read(STDIN_FILENO, &buffer_in, BUFFER_SIZE)){
        fprintf(stdout, "%d Bytes read\n",read_size); fflush(stdout); // debug line
        // len = htonl(read_size + 8);
        // bzero (msg, MSG_SIZE);
        // memcpy(msg, &op, sizeof(op));
        // memcpy(msg+1, &shift, sizeof(shift));
        // memcpy(msg+4, &len, 4);
        // memcpy(msg+8, buffer_in, read_size);
        
        // // TODO: checksum
        
        // // Socket Creation
        // int sockfd;
        // struct sockaddr_in addr;
        
        // sockfd = socket(AF_INET, SOCK_STREAM, 0);
        // if (sockfd == -1){
        //     fprintf(stderr, "socket creation failure: %s\n", strerror(errno));
        //     exit(0);
        // }
        
        // // Socket Binding
        // bzero(&addr, sizeof(addr));
        // addr.sin_family = AF_INET; //IPv4 protocol
        // addr.sin_addr.s_addr = htonl(INADDR_ANY); //IP of client
        // addr.sin_port = htons(PORT);
        
        // if (connect(sockfd, (struct sockaddr *)&host, sizeof(host)) < 0){
        //     fprintf(stderr, "socket connection failure: %s\n", strerror(errno));
        //     exit(0);
        // }else{
        //     // Connection Established
        //     send (sockfd, msg, );
        // }
        
        
    }
}