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