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

#define QUEUE_SIZE 10
#define MSG_SIZE 10000

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
    if (sockfd_listen < 0){
        fprintf(stderr, "socket creation failure: %s\n", strerror(errno));
        exit(0);
    }
    
    // Socket Binding
    bzero(&addr_serv, sizeof(addr_serv));
    addr_serv.sin_family = AF_INET; //IPv4 protocol
    addr_serv.sin_addr.s_addr = htonl(INADDR_ANY); //IP of server
    addr_serv.sin_port = htons(port);
    
    if (bind(sockfd_listen, (struct sockaddr*)&addr_serv, sizeof(addr_serv)) < 0){
        fprintf(stderr, "socket binding failure: %s\n", strerror(errno));
        exit(0);
    }
    
    int state_listen = 0;
    state_listen = listen(sockfd_listen, QUEUE_SIZE);
    
    signal(SIGCHLD, SIG_IGN);
    while(1){
        sockfd_conn = accept(sockfd_listen, (struct sockaddr*)&addr_cli, &len_cli);
        fprintf(stdout, "connection accepted, forking process..\n"); fflush(stdout); // debug line
        int pid = fork();
        if (pid == -1){
            fprintf(stderr, "fork failure: %s\n", strerror(errno));
        }
        if (pid == 0){
            // forked thread
            char *msg = malloc(MSG_SIZE);
            bzero((void *)msg, MSG_SIZE);
            
            if (recv(sockfd_conn, msg, MSG_SIZE, 0) < 0){
                fprintf(stderr, "reception failure: %s\n", strerror(errno));
                exit(0);
            }
            
            fprintf(stdout, "msg received\n"); fflush(stdout); // debug line
            
            // Parse Header
            uint8_t op = (uint8_t) ntohs ( ((uint16_t) *(uint8_t*)msg ) <<8 );
            uint8_t shift = (uint8_t) ntohs ( ((uint16_t) *(uint8_t*)(msg+1)) <<8 );
            uint32_t len = (uint32_t) ntohl (*(uint32_t*)(msg+4));
            fprintf(stdout, "op, shift, len: %d, %d, %d\n", op,shift,len); fflush(stdout); // debug line
            shift = (uint8_t)shift%26;
            
            bzero((void *)(msg + len), MSG_SIZE - len);
            
            // uint16_t ck = checksum(msg, len);
            // if (ck ){
            //     // Checksum verification failure
            //     fprintf(stderr, "checksum verfication failure: %#X\n", ck);
            //     close (sockfd_conn);
            //     continue;
            // }
            
            // Execute Caesar Shift
            // "convert all uppercase letters to lowercase, and perform Caesar cipher only on alphabets"
            for (int i=8; i < len; i++){
                if (i%1 == 0){
                    fprintf(stdout,"msg[%d] : %c(%d)",i, msg[i], (int)msg[i]);fflush(stdout);
                }
                if (isalpha(*(msg + i))){
                    if (isupper(*(msg+i))) msg[i] = (char)tolower(*(msg+i));
                    if (op) {
                        msg[i] = (char) (msg[i] - shift);
                        if (!islower(msg[i])) msg[i] = (char) (msg[i] + 26);
                    }else {
                        msg[i] = (char) (msg[i] + shift);
                        if (!islower(msg[i])) msg[i] = (char) (msg[i] - 26);
                    }
                }
                if (i%1 == 0){
                    fprintf(stdout," -> %c(%d)\n", msg[i], (int)msg[i]);fflush(stdout);
                }
            }
            fwrite(msg + 8, len - 8, 1, stdout); 
            
            // Recalculate checksum
            bzero((void *)(msg+2), 2);
            uint16_t check = checksum(msg, len);
            memcpy(msg+2, &check, sizeof(check));
            
            // Send encrypted/decrypted msg back to the client
            send (sockfd_conn, msg, len, 0);
            free(msg);
        }
        // Close connection
        close (sockfd_conn);
    }
}