## Connection states

The `connection_state` element of a `context_t` object is designated one of the following values.

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

## transport_init()

If the `transport_init()` function is called with `is_active = TRUE`, a SYN packet is sent, and  `connection_state` is set to `CSTATE_SENT`. If `is_active` is set to `False`, `connection_state` is set to `CSTATE_LIST`.        
Then the function enters a loop. At the start of each loop, `stcp_network_recv()` is called, therefore blocking the function until a packet is received. The rest of the connection establishment process - packet sending, receiving, and parsing - is managed in this loop.     
When the 3-way handshake is complete and `connection_state` is set to `CSTATE_ESTB`, the function breaks out of the loop, unblocks the application, and calls `control_loop()`.

## control_loop()

Like the `transport_init()` function, `control_loop()` also has a loop, at the start of which the function `stcp_wait_for_event()` is called. Note that it is called with `NULL` for the `abstime` argument, which means that no timeout is implemented. Managing packet transmissions and state transitions is done inside this loop.

## handling incorrect processes

As stated in https://campuswire.com/c/G9DCC7D11/feed/196 , the code does not take incorrect connection/close processes into consideration. 