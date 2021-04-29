## transport_init()

transport_init() handles all socket features up till connection establishment.  
When the 3-way handshake is complete and `connection_state` is set to `CSTATE_ESTB`, the function breaks out of the loop, unblocks the application, and calls `control_loop()`.

## control_loop()

`control_loop()` iterates through a loop until connection is closed. At the start of each loop, the function `stcp_wait_for_event()` is called. Note that it is called with `NULL` for the `abstime` argument, which means that no timeout is implemented. Packet transmissions (including SFTP) and state transitions are done inside this loop.

## handling incorrect processes

As stated in https://campuswire.com/c/G9DCC7D11/feed/196 , the code does not take incorrect connection/close processes into consideration. 