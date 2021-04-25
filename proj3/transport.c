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
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);

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
    assert(ctx);

    generate_initial_seq_num(ctx);
    ctx->seq_send = ctx->initial_sequence_num;
    
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
        msghdr.th_win = htons(STCP_MSS);
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
                        msghdr.th_win = htons(STCP_MSS);
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
                        msghdr.th_seq = htonl(ctx->seq_send++);
                        msghdr.th_win = htons(STCP_MSS);
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
        // printf("start of a loop, waiting for event..\n");
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
        }
        if (event & NETWORK_DATA){
            stcp_network_recv(sd, buffer, BUFFER_SIZE);
            uint8_t flags_recv = ((struct tcphdr *)buffer)->th_flags;
            tcp_seq ack_recv = ntohl(((struct tcphdr *)buffer)->th_ack);
            tcp_seq seq_recv = ntohl(((struct tcphdr *)buffer)->th_seq);
            // printf("RECV: FLAGS[%d]ACK[%d]SEQ[%d]\n", flags_recv, ack_recv, seq_recv);
            // don't worry about packet loss or reordering
            
            if (flags_recv & TH_ACK){
                ctx->ack_send = seq_recv + 1;
                if (flags_recv & TH_FIN){
                    // printf("FIN received, notifying the application..\n");
                    stcp_fin_received(sd);
                    switch (ctx->connection_state){
                        case CSTATE_ESTB:{
                            // send ACK then goto CSTATE_CLSW
                            msghdr.th_flags = TH_ACK;
                            msghdr.th_seq = htonl(ctx->seq_send++);
                            msghdr.th_win = htons(STCP_MSS);
                            msghdr.th_ack = htonl(ctx->ack_send);
                            stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                            ctx->connection_state = CSTATE_CLSW;
                            break;
                        }
                        case CSTATE_WAIT1:{
                            // send ACK then goto CSTATE_CLSG(sim.close)
                            // or CSTATE_CLSD(non-sim close)
                            if (ack_recv == ctx->seq_send){
                                ctx->connection_state = CSTATE_ESTB;
                            }else{
                                msghdr.th_flags = TH_ACK;
                                msghdr.th_seq = htonl(ctx->seq_send++);
                                msghdr.th_win = htons(STCP_MSS);
                                msghdr.th_ack = htonl(seq_recv + 1);
                                stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                                ctx->connection_state = CSTATE_ESTB;
                            }
                            break;
                        }
                        case CSTATE_WAIT2:{
                            // send ACK then goto CSTATE_CLSD
                            msghdr.th_flags = TH_ACK;
                            msghdr.th_seq = htonl(ctx->seq_send++);
                            msghdr.th_win = htons(STCP_MSS);
                            msghdr.th_ack = htonl(seq_recv + 1);
                            stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
                            ctx->connection_state = CSTATE_ESTB;
                            break;
                        }
                        default:{
                            break;
                        }
                    }
                }
                else{
                    switch (ctx->connection_state){
                        case CSTATE_WAIT1:{
                            // goto CSTATE_WAIT2
                            ctx->connection_state = CSTATE_WAIT2;
                            break;
                        }
                        case CSTATE_CLSG:{
                            // goto CSTAE_CLSD
                            ctx->connection_state = CSTATE_CLSD;
                            break;
                        }
                        default:{
                            break;
                        }
                    }
                }
            }
        }
        else if (event & APP_CLOSE_REQUESTED){
            // send FIN|ACK then goto CSTATE_WAIT1
            msghdr.th_flags = TH_FIN|TH_ACK;
            msghdr.th_seq = htonl(ctx->seq_send++);
            msghdr.th_win = htons(STCP_MSS);
            msghdr.th_ack = htonl(ctx->ack_send);
            stcp_network_send(sd, &msghdr, sizeof(struct tcphdr), NULL);
            ctx->connection_state = CSTATE_WAIT1;
            break;
        }
        
        ctx->done = (ctx->connection_state == CSTATE_CLSD)? TRUE: FALSE;
        /* etc. */
    }
    // printf("Connection Closed\n");
    free(buffer);
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



