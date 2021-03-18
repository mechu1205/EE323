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

#define MSG_SIZE 1000000 //10M = 10000000
#define HEADER_SIZE 8
#define BUFFER_SIZE (MSG_SIZE-HEADER_SIZE)

#define PORT 8888

uint16_t checksum(const char *buf, uint32_t size){
    // https://locklessinc.com/articles/tcp_checksum/
	uint64_t sum = 0;
	const uint64_t *b = (uint64_t *) buf;

	uint32_t t1, t2;
	uint16_t t3, t4;

	/* Main loop - 8 bytes at a time */
	while (size >= sizeof(uint64_t))
	{
		uint64_t s = *b++;
		sum += s;
		if (sum < s) sum++;
		size -= 8;
	}

	/* Handle tail less than 8-bytes long */
	buf = (const char *) b;
	if (size & 4)
	{
		uint32_t s = *(uint32_t *)buf;
		sum += s;
		if (sum < s) sum++;
		buf += 4;
	}

	if (size & 2)
	{
		uint16_t s = *(uint16_t *) buf;
		sum += s;
		if (sum < s) sum++;
		buf += 2;
	}

	if (size)
	{
		uint8_t s = *(uint8_t *) buf;
		sum += s;
		if (sum < s) sum++;
	}

	/* Fold down to 16 bits */
	t1 = sum;
	t2 = sum >> 32;
	t1 += t2;
	if (t1 < t2) t1++;
	t3 = t1;
	t4 = t1 >> 16;
	t3 += t4;
	if (t3 < t4) t3++;

	return ~t3;
}

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
    
    // fprintf(stdout, "host, port, op, shift: %s, %d, %d, %d\n", host,port,op,shift); fflush(stdout); // debug line
    
    char *buffer_in = malloc(BUFFER_SIZE);
    char *msg;
    char *buffer_recv = malloc(MSG_SIZE);
    int read_size = 0;
    
    // Loop until EOF is reached
    while(read_size = read(STDIN_FILENO, buffer_in, BUFFER_SIZE) ){
        bzero(buffer_recv, MSG_SIZE);
        
        // fprintf(stdout, "loop start\n"); fflush(stdout); // debug line
        // fprintf(stdout, "%d Bytes read\n",read_size); fflush(stdout); // debug line
        uint32_t len_h = read_size + HEADER_SIZE;
        msg = malloc(len_h);
        bzero(msg, len_h);
        uint32_t len_n = htonl(len_h);
        memcpy(msg, &op, sizeof(op));
        // fprintf(stdout, "op: %d (should be %d)\n", *((uint8_t*)msg), op); fflush(stdout); // debug line
        memcpy(msg+1, &shift, sizeof(shift));
        // fprintf(stdout, "shift: %d (should be %d)\n", *((uint8_t*)(msg+1)), shift); fflush(stdout); // debug line
        memcpy(msg+4, &len_n, 4);
        // fprintf(stdout, "len: %d (should be %d)\n", *((uint32_t*)(msg+4)), len); fflush(stdout); // debug line
        memcpy(msg+8, buffer_in, read_size);
        // fprintf(stdout, "memcpy finished\n"); fflush(stdout); // debug line
        
        uint16_t check = checksum(msg, read_size + HEADER_SIZE);
        memcpy(msg+2, &check, sizeof(check));
        
        // Socket Creation
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1){
            fprintf(stderr, "socket creation failure: %s\n", strerror(errno));
            exit(-1);
        }
        
        addr_host.sin_family = AF_INET; //IPv4 protocol
        inet_pton(AF_INET, host, &addr_host.sin_addr);
        addr_host.sin_port = port;
        
        // fprintf(stdout, "attempting connection..\n"); fflush(stdout); // debug line
        if (connect(sockfd, (struct sockaddr *)&addr_host, sizeof(addr_host)) < 0){
            fprintf(stderr, "socket connection failure: %s\n", strerror(errno));
            exit(-1);
        }
        
        // Send message
        // fprintf(stdout, "connection established, sending msg..\n"); fflush(stdout); // debug line
        send (sockfd, msg, read_size + HEADER_SIZE, 0);
        
        // Wait for response (before reading from stdin or sending another msg)
        // fprintf(stdout, "msg sent, waiting for response..\n"); fflush(stdout); // debug line
        
        int len_msg = 0;
        int len_recv = 0;
        free(msg);
        msg = malloc(MSG_SIZE);
        bzero(msg, MSG_SIZE);

        // read sockfd to buffer_recv, and move it to msg        
        do {
            len_recv = recv(sockfd, buffer_recv, MSG_SIZE, 0);
            if (len_recv < 0){
                fprintf(stderr, "reception failure: %s\n", strerror(errno));
                exit(-1);
            }
            memcpy(msg + len_msg, buffer_recv, len_recv);
            len_msg += len_recv;
            // fprintf(stdout, "received %d B (total %d B)\n", len_recv, len_msg); fflush(stdout);
        }while (len_msg <  read_size + HEADER_SIZE);
        
        
        // Checksum verification
        len_h = (uint32_t) ntohl (*(uint32_t*)(msg+4));
        if ( checksum(msg, len_h)){
            // Checksum verification failure
            fprintf(stderr, "checksum verfication failure\n");
            continue;
        }
        
        // fprintf(stdout, "msg received, printing msg..\n"); fflush(stdout); // debug line
        fwrite(msg + HEADER_SIZE, len_h - HEADER_SIZE, 1, stdout); fflush(stdout);
        // fprintf(stdout, "\nend of msg\n"); fflush(stdout); // debug line
        free(msg);
        // fprintf(stdout, "closing connection..\n"); fflush(stdout); // debug line
        close(sockfd);
        // fprintf(stdout, "connection closed, exiting loop\n"); fflush(stdout); // debug line
        
    }
    // fprintf(stdout, "exiting..\n"); fflush(stdout); // debug line
    free(buffer_in);
    free(buffer_recv);
    return 0;
}