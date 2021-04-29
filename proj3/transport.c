/*
 * transport.c 
 *
 * EE323 HW#3 (Reliable Transport)
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. You are required to fill in the STCP
 * functionality in this file. 
 *
 */


#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <netinet/in.h>
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"
// #include "tcp_sum.h"
#include "mysock_impl.h"


// TODO: Add states to be used as the contextual states of TCP handshaking
// ex) CSTATE_LISTEN, CSTATE_SYN_SENT, ...
enum {
    CSTATE_CLSD = 0,
    CSTATE_LIST,
    CSTATE_RCVD,
    CSTATE_SENT,
    CSTATE_ESTB,
    CSTATE_CLSW,
    CSTATE_LACK,
    CSTATE_WAIT1,
    CSTATE_WAIT2,
    CSTATE_CLSG,
};    /* obviously you should have more states */

enum {
    CONG_NOCONNECT = 0, // before connection is established
    CONG_SLOWSTART,
    CONG_AVOIDANCE,
    CONG_FASTRECOV,
};

#define BUFFER_SIZE (65536)

// TODO: Add your own variables helping your context management
/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;

    /* any other connection-wide global variables go here */
    tcp_seq seq_send;
    tcp_seq ack_send;
    tcp_seq ack_recv_max;
    
    int congestion_state;
    int cwnd;
    int rwnd;
    int ssthresh;
    int rwnd_rmt;
    
    int buf_send_cap;
    int buf_send_size;
    char *buf_send;
    
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);
void update_buf_send_cap(context_t *ctx);

/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    // printf("transport_init(%d, %d)\n", sd, is_active);
    context_t *ctx;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    // calloc initiliazes all bits to 0
    // thus connection_state is automatically set to CSTATE_CLSD
    // congestion_state is set to CONG_NOCONNECT
    assert(ctx);

    generate_initial_seq_num(ctx);
    ctx->seq_send = ctx->initial_sequence_num;
    
    ctx->cwnd = STCP_MSS;
    ctx->rwnd = 3072; // fixed
    ctx->ssthresh = 4*STCP_MSS;
    ctx->rwnd_rmt = -1;
    ctx->buf_send_cap = ctx->cwnd;
    ctx->buf_send_size = 0;
    ctx->buf_send = malloc(ctx->buf_send_cap); // realloc()ed by congestion control
    update_buf_send_cap(ctx);
    
    struct tcphdr msghdr;
    bzero(&msghdr, sizeof(struct tcphdr));
    void *buffer = calloc(1, BUFFER_SIZE);
    
    /* TODO: you should send a SYN packet here if is_active, or wait for one
     * to arrive if !is_active.  after the handshake completes, unblock the
     * application with stcp_unblock_application(sd).  you may also use
     * this to communicate an error condition back to the application, e.g.
     * if connection fails; to do so, just set errno appropriately (e.g. to
     * ECONNREFUSED, etc.) before calling the function.
     */
    if(is_active){
        // printf("Initiating Active Open..\n");
        // Active Open; send SYN and wait
        // if recv SYN|ACK, send ACK
        // if recv SYN, send SYN_ACK and wait for ACK
        msghdr.th_flags = TH_SYN;
        msghdr.th_seq = htonl(ctx->seq_send++);
        msghdr.th_win = htons(ctx->rwnd);
        if( stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL) < 0 ){
            //close
            free(ctx);
            free(buffer);
            stcp_unblock_application(sd);
            return;
        };
        ctx->connection_state = CSTATE_SENT;
        // checksum set by stcp_network_send
    }else{
        // printf("Initiating Passive Open..\n");
        // Passive open
        ctx->connection_state = CSTATE_LIST;
    }
    
    // int wait_for_arrival = 1;
    int do_close = 0;
    
    while(ctx->connection_state != CSTATE_ESTB){
        // printf("Waiting for incoming packet..\n");
        // Wait for incoming message
        ssize_t len_recv = stcp_network_recv(sd, buffer, BUFFER_SIZE);
        // checksum verification is done by stcp_network_recv
        uint8_t flags_recv = ((struct tcphdr *)buffer)->th_flags;
        tcp_seq ack_recv = ntohl(((struct tcphdr *)buffer)->th_ack);
        tcp_seq seq_recv = ntohl(((struct tcphdr *)buffer)->th_seq);
        // printf("RECV: FLAGS[%d]ACK[%d]SEQ[%d]\n", flags_recv, ack_recv, seq_recv);
        // printf("ack_recv = %d, seq_send = %d\n", ack_recv, ctx->seq_send);
        if ((ack_recv != ctx->seq_send) && (flags_recv & TH_ACK)){
            // printf("do_close\n");
            do_close = 1;
        }
        ctx->ack_send = seq_recv + 1;
        if (!do_close){
            switch (ctx->connection_state){
                case CSTATE_LIST:{
                    // wait for SYN then send SYN|ACK, goto CSTATE_RCVD
                    if (flags_recv == TH_SYN){
                        bzero(&msghdr, sizeof(struct tcphdr));
                        msghdr.th_flags = TH_SYN|TH_ACK;
                        msghdr.th_seq = htonl(ctx->seq_send++);
                        msghdr.th_win = htons(ctx->rwnd);
                        msghdr.th_ack = htonl(ctx->ack_send);
                        // printf("Sending..\n");
                        stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                        ctx->connection_state = CSTATE_RCVD;
                    }else{
                        do_close = 1;
                    }
                    break;
                }
                case CSTATE_SENT:{
                    // wait for SYN|ACK then send ACK, goto CSTATE_ESTB
                    if (flags_recv == (TH_SYN | TH_ACK)){
                        bzero(&msghdr, sizeof(struct tcphdr));
                        msghdr.th_flags = TH_ACK;
                        msghdr.th_seq = htonl(ctx->seq_send);
                        msghdr.th_win = htons(ctx->rwnd);
                        msghdr.th_ack = htonl(ctx->ack_send);
                        stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                        ctx->connection_state = CSTATE_ESTB;
                    }else{
                        do_close = 1;
                    }
                    break;
                }
                case CSTATE_RCVD:{
                    // wait for ACK then goto CSTAT_ESTB
                    if (flags_recv == TH_ACK){
                        ctx->connection_state = CSTATE_ESTB;
                    }else{
                        do_close = 1;
                    }
                    break;
                }
                default:{
                    do_close = 1;
                }
            }
        }
        if (do_close){
            // printf("Closing without finishing connection..\n");
            //close
            free(ctx);
            free(buffer);
            stcp_unblock_application(sd);
            return;
        }
    }
    free(buffer);
    // printf("unblocking application with sockfd %d\n", sd);
    stcp_unblock_application(sd);
    // printf("unblocked application with sockfd %d, entering control loop\n", sd);
    control_loop(sd, ctx);
    // printf("application with sockfd %d exited from control loop\n", sd);
    /* do any cleanup here */
    free(ctx);
}

// DO NOT MODIFY THIS FUNCTION
/* generate initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);
    ctx->initial_sequence_num = 1;
}


/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    // printf("starting control loop.. (sockfd %d)\n", sd);
    assert(ctx);
    void *buffer = calloc(1, BUFFER_SIZE);
    struct tcphdr msghdr;
        
    while (!ctx->done)
    {
        // printf("start of a loop, waiting for event..\n"); fflush(stdout);
        // printf("Connection state: %d\n", ctx->connection_state);
        // bzero(buffer, BUFFER_SIZE);
        bzero(&msghdr, sizeof(struct tcphdr));
        unsigned int event;

        /* see stcp_api.h or stcp_api.c for details of this function */
        /* TODO: you will need to change some of these arguments! */
        event = stcp_wait_for_event(sd, ANY_EVENT, NULL);
        // printf("event returned\n");

        /* check whether it was the network, app, or a close request */
        // Handle the cases where the events are APP_DATA, APP_CLOSE_REQUESTED, NETWORK_DATA
        if (event & APP_DATA)
        {
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
            printf("app: data transfer requested\n"); fflush(stdout);
            printf("data in send-buffer: %dB\nreceiving data from app..\n", ctx->buf_send_size); fflush(stdout);
            char *buf_ptr = ctx->buf_send + ctx->buf_send_size;
            printf("ctx->buf_send: 0x%x\n", ctx->buf_send); fflush(stdout);
            ctx->buf_send_size += stcp_app_recv(
                sd,
                ctx->buf_send + ctx->buf_send_size,
                ctx->buf_send_cap - ctx->buf_send_size
            );
            printf("data in send-buffer: %dB\n", ctx->buf_send_size); fflush(stdout);
            // send up to MIN(cwnd, rwnd_mt) amount of data in each RT
            // i.e. send entire buffer
            while(ctx->buf_send_size > 0){
                //send up to STCP_MSS amount of data in each packet
                int data_size = MIN (ctx->buf_send_size, STCP_MSS);
                printf("sending %dB..\n",data_size); fflush(stdout);
                bzero(&msghdr, sizeof(struct tcphdr));
                msghdr.th_flags = TH_ACK;
                msghdr.th_seq = htonl(ctx->seq_send);
                msghdr.th_win = htons(ctx->rwnd);
                msghdr.th_ack = htonl(ctx->ack_send);
                
                // bzero(buffer, BUFFER_SIZE);
                memcpy(buffer, &msghdr, sizeof(struct tcphdr));
                memcpy(buffer + sizeof(struct tcphdr), buf_ptr, data_size);
                buf_ptr += data_size;
                stcp_network_send(sd, buffer, sizeof(struct tcphdr) + data_size, NULL);
                ctx->buf_send_size -= data_size;
                
                ctx->seq_send += data_size;
                printf("data in send-buffer: %dB\n", ctx->buf_send_size); fflush(stdout);
            }
        }
        if (event & NETWORK_DATA){
            int data_size = stcp_network_recv(sd, buffer, BUFFER_SIZE) - sizeof(struct tcphdr);
            uint8_t flags_recv = ((struct tcphdr *)buffer)->th_flags;
            tcp_seq ack_recv = ntohl(((struct tcphdr *)buffer)->th_ack);
            tcp_seq seq_recv = ntohl(((struct tcphdr *)buffer)->th_seq);
            // printf("RECV: FLAGS[%d]ACK[%d]SEQ[%d]\n", flags_recv, ack_recv, seq_recv); fflush(stdout);
            // don't worry about packet loss or reordering
            
            if (flags_recv & TH_ACK){
                ctx->ack_send = seq_recv + data_size;
                if (ack_recv > ctx->ack_recv_max){
                    // update cwnd
                    /* Since reliable channel is assumed,
                    * the only case of cong_state change is
                    * slow start to congestion avoidance
                    */
                    ctx->ack_recv_max = ack_recv;
                    switch(ctx->congestion_state){
                        case CONG_SLOWSTART:{
                            ctx->cwnd += STCP_MSS;
                            if (ctx->cwnd >= ctx->ssthresh)
                                ctx->congestion_state = CONG_AVOIDANCE;
                            break;
                        }
                        case CONG_AVOIDANCE:{
                            ctx->cwnd += (int) (STCP_MSS * (STCP_MSS / ctx->cwnd));
                            break;
                        }
                        default:{
                            // should not be reached
                        }
                    }
                    update_buf_send_cap(ctx);
                }
                
                if (flags_recv & TH_FIN){
                    ctx->ack_send ++;
                    stcp_fin_received(sd);
                    // printf("FIN received, app notified\n"); fflush(stdout);
                    switch (ctx->connection_state){
                        case CSTATE_ESTB:{
                            // send FIN|ACK then goto CSTATE_CLSW
                            msghdr.th_flags = TH_ACK;
                            msghdr.th_seq = htonl(ctx->seq_send);
                            msghdr.th_win = htons(ctx->rwnd);
                            msghdr.th_ack = htonl(ctx->ack_send);
                            stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                            ctx->connection_state = CSTATE_CLSW;
                            break;
                        }
                        case CSTATE_WAIT1:{
                            // send ACK then goto CSTATE_CLSG(sim.close)
                            // or CSTATE_CLSD(non-sim close)
                            if (ack_recv == ctx->seq_send){
                                ctx->connection_state = CSTATE_CLSD;
                            }else{
                                msghdr.th_flags = TH_ACK;
                                msghdr.th_seq = htonl(ctx->seq_send++);
                                msghdr.th_win = htons(ctx->rwnd);
                                msghdr.th_ack = htonl(seq_recv + 1);
                                stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                                ctx->connection_state = CSTATE_CLSG;
                            }
                            break;
                        }
                        case CSTATE_WAIT2:{
                            // send ACK then goto CSTATE_CLSD
                            msghdr.th_flags = TH_ACK;
                            msghdr.th_seq = htonl(ctx->seq_send);
                            msghdr.th_win = htons(ctx->rwnd);
                            msghdr.th_ack = htonl(seq_recv + 1);
                            stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                            ctx->connection_state = CSTATE_CLSD;
                            break;
                        }
                        default:{
                            break;
                        }
                    }
                }
                else{
                    if (data_size > 0){
                        // data received; send ACK to remote and pass data to app
                        bzero(&msghdr, sizeof(struct tcphdr));
                        msghdr.th_flags = TH_ACK;
                        msghdr.th_seq = htonl(ctx->seq_send);
                        msghdr.th_win = htons(ctx->rwnd);
                        msghdr.th_ack = htonl(ctx->ack_send);
                        stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                        
                        stcp_app_send(sd, buffer + sizeof(struct tcphdr), data_size);
                    }
                    
                    switch (ctx->connection_state){
                        case CSTATE_WAIT1:{
                            // goto CSTATE_WAIT2
                            ctx->connection_state = CSTATE_WAIT2;
                            break;
                        }
                        case CSTATE_CLSG:{
                            // goto CSTATE_CLSD
                            ctx->connection_state = CSTATE_CLSD;
                            break;
                        }
                        case CSTATE_LACK:{
                            // goto CSTATE_CLSD
                            ctx->connection_state = CSTATE_CLSD;
                        }
                        default:{
                            break;
                        }
                    }
                }
            }
        }
        else if (event & APP_CLOSE_REQUESTED){
            // printf("app: close requested\n"); fflush(stdout);
            switch (ctx->connection_state){
                case CSTATE_ESTB:{
                    // send FIN|ACK then goto CSTATE_WAIT1
                    msghdr.th_flags = TH_FIN|TH_ACK;
                    msghdr.th_seq = htonl(ctx->seq_send++);
                    msghdr.th_win = htons(ctx->rwnd);
                    msghdr.th_ack = htonl(ctx->ack_send);
                    stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                    ctx->connection_state = CSTATE_WAIT1;
                    break;
                }
                case CSTATE_CLSW:{
                    // send FIN|ACK then goto CSTATE_LACK
                    msghdr.th_flags = TH_FIN|TH_ACK;
                    msghdr.th_seq = htonl(ctx->seq_send++);
                    msghdr.th_win = htons(ctx->rwnd);
                    msghdr.th_ack = htonl(ctx->ack_send);
                    stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                    ctx->connection_state = CSTATE_LACK;
                    break;
                }
                default:{
                    break;
                }
            }
        }
        
        ctx->done = (ctx->connection_state == CSTATE_CLSD)? TRUE: FALSE;
        // printf("connection state: %d\n", ctx->connection_state); fflush(stdout);
    }
    // printf("Connection Closed\n");
    free(buffer);
}

void update_buf_send_cap(context_t *ctx){
    ctx->buf_send_cap = (ctx->rwnd_rmt > 0) ? MIN(STCP_MSS, ctx->rwnd_rmt) : ctx->cwnd;
    realloc(ctx->buf_send, ctx->buf_send_cap);
    printf("send-buffer reallocated to 0x%x (cap: %dB)\n ", ctx->buf_send, ctx->buf_send_cap); fflush(stdout);
}

/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_printf(const char *format,...)
{
    va_list argptr;
    char buffer[1024];

    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}



