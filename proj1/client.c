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


int main (int argc, char* argv[]){
    // argument parser, use getopt
    // -o: op, -s: shift, -p: port, -h: host
    
    // Argument Parsing
    int input_correct = 0;
    struct sockaddr_in host;
    uint16_t port;
    int op, shift;
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
                op = atoi(optarg);
                input_correct ++;
                break;
            case 's':
                shift = atoi(optarg);
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
    
}