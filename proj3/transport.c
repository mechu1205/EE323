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
#include "tcp_sum.h"
#include "mysock_impl.h"


// TODO: Add states to be used as the contextual states of TCP handshaking
// ex) CSTATE_LISTEN, CSTATE_SYN_SENT, ...
enum {
    CSTATE_CLSD = 0,
    CSTATE_LIST,
    CSTATE_RCVD,
    CSTATE_SENT,
    CSTATE_ESTB,
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
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);

/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    // calloc initiliazes all bits to 0
    // thus connection_state is automatically set to CSTATE_CLSD
    assert(ctx);

    generate_initial_seq_num(ctx);
    tcp_seq seq_send = ctx->initial_sequence_num;
    int san = 0; //sanity checks
    
    mysock_context_t *context = _mysock_get_context(sd);
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
        // Active Open; send SYN and wait
        // if recv SYN|ACK, send ACK
        // if recv SYN, send SYN_ACK and wait for ACK
        msghdr.th_flags = TH_SYN;
        msghdr.th_seq = htonl(seq_send++);
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
        // Passive open
        ctx->connection_state = CSTATE_RCVD;
    }
    
    // int wait_for_arrival = 1;
    int do_close = 0;
    
    while(ctx->connection_state != CSTATE_ESTB){
        // Wait for incoming message
        ssize_t len_recv = stcp_network_recv(sd, buffer, BUFFER_SIZE);
        // checksum verification is done by stcp_network_recv
        uint8_t flags_recv = ((struct tcphdr *)buffer)->th_flags;
        tcp_seq ack_recv = ntohl(((struct tcphdr *)buffer)->th_ack);
        tcp_seq seq_recv = ntohl(((struct tcphdr *)buffer)->th_seq);
        if (ack_recv != seq_send){
            do_close = 1;
        }
        if (!do_close){
            switch (ctx->connection_state){
                case CSTATE_LIST:{
                    // wait for SYN then send SYN|ACK, goto CSTATE_RCVD
                    if (flags_recv == TH_SYN){
                        bzero(&msghdr, sizeof(struct tcphdr));
                        msghdr.th_flags = TH_SYN|TH_ACK;
                        msghdr.th_seq = htonl(seq_send++);
                        msghdr.th_win = htons(STCP_MSS);
                        msghdr.th_ack = htonl(seq_recv + 1);
                        stcp_network_send(sd, &msghdr, sizeof(struct tcphdr));
                        ctx->connection_state = CSTATE_RCVD;
                    }else{
                        do_close = 1;
                    }
                    break;
                }
                case CSTATE_SENT:{
                    // wait for SYN|ACK then send ACK, goto CSTATE_ESTB
                    if (flags_recv == TH_SYN | TH_ACK){
                        bzero(&msghdr, sizeof(struct tcphdr));
                        msghdr.th_flags = TH_ACK;
                        msghdr.th_seq = htonl(seq_send++);
                        msghdr.th_win = htons(STCP_MSS);
                        msghdr.th_ack = htonl(seq_recv + 1);
                        stcp_network_send(sd, &msghdr, sizeof(struct tcphdr));
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
            //close
            free(ctx);
            free(buffer);
            stcp_unblock_application(sd);
            return;
        }
    }
    free(buffer);
    
    stcp_unblock_application(sd);

    control_loop(sd, ctx);

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

    while (!ctx->done)
    {
        unsigned int event;

        /* see stcp_api.h or stcp_api.c for details of this function */
        /* TODO: you will need to change some of these arguments! */
        event = stcp_wait_for_event(sd, ANY_EVENT, NULL);

        /* check whether it was the network, app, or a close request */
        // Handle the cases where the events are APP_DATA, APP_CLOSE_REQUESTED, NETWORK_DATA
        if (event & APP_DATA)
        {
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
        }

        /* etc. */
    }
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
void our_dprintf(const char *format,...)
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



