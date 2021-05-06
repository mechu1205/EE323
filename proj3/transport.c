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
    int buf_send_start;
    int buf_send_end;
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
    ctx->rwnd_rmt = 3072; // fixed (no mechanism to update this value)
    ctx->buf_send_cap = ctx->cwnd;
    ctx->buf_send_start = 0;
    ctx->buf_send_end = 0;
    ctx->buf_send = malloc(ctx->buf_send_cap); // realloc()ed by congestion control
    
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
        // printf("ack_recv_max = %d, seq_send = %d, ack_send = %d\n", ctx->ack_recv_max, ctx->seq_send, ctx->ack_send);
        // printf("Waiting for incoming packet..\n");
        // Wait for incoming message
        ssize_t len_recv = stcp_network_recv(sd, buffer, BUFFER_SIZE);
        // checksum verification is done by stcp_network_recv
        uint8_t flags_recv = ((struct tcphdr *)buffer)->th_flags;
        tcp_seq ack_recv = ntohl(((struct tcphdr *)buffer)->th_ack);
        tcp_seq seq_recv = ntohl(((struct tcphdr *)buffer)->th_seq);
        // printf("RECV: FLAGS[%d]ACK[%d]SEQ[%d]\n", flags_recv, ack_recv, seq_recv);
        if ((ack_recv != ctx->seq_send) && (flags_recv & TH_ACK)){
            // printf("do_close\n");
            do_close = 1;
        }
        ctx->ack_send = seq_recv; // if SYN is included, increase further by 1
        if (!do_close){
            switch (ctx->connection_state){
                case CSTATE_LIST:{
                    // wait for SYN then send SYN|ACK, goto CSTATE_RCVD
                    if (flags_recv == TH_SYN){
                        bzero(&msghdr, sizeof(struct tcphdr));
                        msghdr.th_flags = TH_SYN|TH_ACK;
                        msghdr.th_seq = htonl(ctx->seq_send++);
                        msghdr.th_win = htons(ctx->rwnd);
                        msghdr.th_ack = htonl(++ctx->ack_send);
                        // printf("Sending..\n");
                        stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                        ctx->connection_state = CSTATE_RCVD;
                    }else{
                        do_close = 1;
                    }
                    break;
                }
                case CSTATE_SENT:{
                    // wait for SYN|ACK then send ACK, goto CSTATE_ESTB (non-simul open)
                    // or wait for SYN then send SYN|ACK, goto CSTATE_RCVD (simul open)
                    if (flags_recv == (TH_SYN | TH_ACK)){
                        bzero(&msghdr, sizeof(struct tcphdr));
                        msghdr.th_flags = TH_ACK;
                        msghdr.th_seq = htonl(ctx->seq_send);
                        msghdr.th_win = htons(ctx->rwnd);
                        msghdr.th_ack = htonl(++ctx->ack_send);
                        stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                        ctx->connection_state = CSTATE_ESTB;
                        ctx->ack_recv_max = ack_recv;
                    }else if (flags_recv == TH_SYN){
                        bzero(&msghdr, sizeof(struct tcphdr));
                        msghdr.th_flags = TH_SYN|TH_ACK;
                        msghdr.th_seq = htonl(ctx->seq_send - 1); // seq_send not increased
                        msghdr.th_win = htons(ctx->rwnd);
                        msghdr.th_ack = htonl(++ctx->ack_send);
                        stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                        ctx->connection_state = CSTATE_RCVD;
                    }else{
                        do_close = 1;
                    }
                    break;
                }
                case CSTATE_RCVD:{
                    // wait for ACK then goto CSTAT_ESTB
                    if (flags_recv & TH_ACK){ // AND instead of EQUALS to include simul. open
                        ctx->connection_state = CSTATE_ESTB;
                        ctx->ack_recv_max = ack_recv;
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
    ctx->congestion_state = CONG_SLOWSTART;
    // printf("Connection Established; ack_recv_max:%d seq_send:%d ack_send:%d\n", ctx->ack_recv_max, ctx->seq_send, ctx->ack_send);
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
    assert(ctx);
    void *buffer = calloc(1, BUFFER_SIZE);
    struct tcphdr msghdr;
        
    while (!ctx->done)
    {
        // printf("start of a loop\n"); fflush(stdout);
        // printf("Connection state: %d\n", ctx->connection_state);
        // bzero(buffer, BUFFER_SIZE);
        bzero(&msghdr, sizeof(struct tcphdr));
        // printf("zeroed header\n");
        unsigned int event;
        // printf("waiting for an event..\n"); fflush(stdout);
        /* see stcp_api.h or stcp_api.c for details of this function */
        /* TODO: you will need to change some of these arguments! */
        if (ctx->buf_send_end < ctx->buf_send_cap){
            // printf("all events will be returned for this loop\n");
            event = stcp_wait_for_event(sd, ANY_EVENT, NULL);
        }else{
            // printf("APP_DATA will be ignored for this loop\n");
            event = stcp_wait_for_event(sd, ANY_EVENT - APP_DATA, NULL);
        }
        // event = stcp_wait_for_event(sd, ANY_EVENT, NULL);
        // printf("event returned\n");

        /* check whether it was the network, app, or a close request */
        // Handle the cases where the events are APP_DATA, APP_CLOSE_REQUESTED, NETWORK_DATA
        if (event & APP_DATA)
        {
            // printf("APP_DATA\n"); fflush(stdout);
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
            int newdata_size = stcp_app_recv(
                sd,
                ctx->buf_send + ctx->buf_send_end,
                ctx->buf_send_cap - ctx->buf_send_end
            );
            // printf("data copied from app to send-buffer: %dB\n", newdata_size); fflush(stdout);
            // transmit new data just received from app
            while(newdata_size > 0){
                //send up to STCP_MSS amount of data in each packet
                int data_size = MIN (newdata_size, STCP_MSS);
                // printf("sending %dB..\n",data_size); fflush(stdout);
                bzero(&msghdr, sizeof(struct tcphdr));
                msghdr.th_flags = TH_ACK;
                msghdr.th_seq = htonl(ctx->seq_send);
                msghdr.th_win = htons(ctx->rwnd);
                msghdr.th_ack = htonl(ctx->ack_send);
                
                // bzero(buffer, BUFFER_SIZE);
                memcpy(buffer, &msghdr, sizeof(struct tcphdr));
                memcpy(buffer + sizeof(struct tcphdr), ctx->buf_send + ctx->buf_send_end, data_size);
                ctx->buf_send_end += data_size;
                stcp_network_send(sd, buffer, sizeof(struct tcphdr) + data_size, NULL);
                ctx->seq_send += data_size;
                newdata_size -= data_size;
            }
        }
        if (event & NETWORK_DATA){
            // printf("NETWORK_DATA\n"); fflush(stdout);
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
                    // printf("New ACK number received: %d -> %d\n", ctx->ack_recv_max, ack_recv); fflush(stdout);
                    ctx->buf_send_start += (ack_recv - ctx->ack_recv_max);
                    /* FIN also causes an increment of ack number to-be-received.
                     * Reply for FIN will increase buf_send_start by 1 more than the correct amount.
                     * Therefore buf_send_start is compared to buf_send_end to handle FIN arrival.
                     */
                    if (ctx->buf_send_start > ctx->buf_send_end) ctx->buf_send_start = ctx->buf_send_end;
                    for(int buf_send_offset = ctx->buf_send_start;
                        buf_send_offset < ctx->buf_send_end;
                        buf_send_offset++){
                        ctx->buf_send[buf_send_offset - ctx->buf_send_start] = ctx->buf_send[buf_send_offset];
                    }
                    ctx->buf_send_end -= ctx->buf_send_start;
                    ctx->buf_send_start = 0;
                    // printf("data on-the-fly: %dB\n", ctx->buf_send_end - ctx->buf_send_start);
                    ctx->ack_recv_max = ack_recv;
                    switch(ctx->congestion_state){
                        case CONG_SLOWSTART:{
                            ctx->cwnd += STCP_MSS;
                            if (ctx->cwnd >= ctx->ssthresh)
                                ctx->congestion_state = CONG_AVOIDANCE;
                            break;
                        }
                        case CONG_AVOIDANCE:{
                            ctx->cwnd += (int) ((STCP_MSS * STCP_MSS) / ctx->cwnd);
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
                            // send ACK then goto CSTATE_CLSW
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
                        // printf("data received, sending reply..\n", ctx->ack_recv_max, ack_recv); fflush(stdout);
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
        if (event & APP_CLOSE_REQUESTED){
            /* because of how stcp_wait_for_event is implemented,
             * APP_CLOSED_REQUESTED is never set with APP_DATA
             */
            
            // printf("APP_CLOSE_REQUESTED\n"); fflush(stdout);
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
            // printf("FIN sent\n");
        }
        
        ctx->done = (ctx->connection_state == CSTATE_CLSD)? TRUE: FALSE;
        // printf("connection state: %d\n", ctx->connection_state); fflush(stdout);
    }
    // printf("Connection Closed\n");
    free(buffer);
}

/* Updates ctx->buf_send_cap to MIN(ctx->cwnd, ctx->rwnd_rmt) and if it causes
 * a change of value, reallocate ctx->buf_send appropriately.
 * Note that if ctx->buf_send_cap was to decrease, data might be lost.
 * This will not happen if reliable network is guaranteed.
 */
void update_buf_send_cap(context_t *ctx){
    int old_cap = ctx->buf_send_cap;
    ctx->buf_send_cap = (ctx->rwnd_rmt > 0) ? MIN(ctx->cwnd, ctx->rwnd_rmt) : ctx->cwnd;
    if(old_cap != ctx->buf_send_cap) ctx->buf_send = realloc(ctx->buf_send, ctx->buf_send_cap);
    // printf("send-buffer cap updated: %dB -> %dB\n", old_cap, ctx->buf_send_cap);
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



