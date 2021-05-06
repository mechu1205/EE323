## transport_init()

transport_init() handles all socket features up till connection establishment.  
When the 3-way handshake is complete and `connection_state` is set to `CSTATE_ESTB`, the function breaks out of the loop, unblocks the application, and calls `control_loop()`.

## control_loop()

`control_loop()` iterates through a loop until connection is closed. At the start of each loop, the function `stcp_wait_for_event()` is called. Note that it is called with `NULL` for the `abstime` argument, which means that no timeout is implemented. Packet transmissions (including SFTP) and state transitions are done inside this loop.

## data send-buffer control

When there is data to be sent, it is written to `buf_send`. The capacity of `buf_send` is decided as the smaller value of `cwnd` and 3072, and will eventually converge to 3072.
When the app requests that data be sent, the data is copied to `buf_send`, starting from `buf_send[buf_send_end]`, up to `buf_send_cap - buf_send_end` bytes, and then immediately sent to the connected peer in packets (each of which contains at most 536 bytes of data, excluding the header).      
Arrival of packets with increased ACK numbers mean that the peer has safely received additional data. The sender needs not keep track of data that is ack'ed by the peer. Therefore, on such events, `buf_send` shifts its contents by the amount of newly ack'ed data. This will reduce `buf_send_end`, allowing the socket to receive more data from the app.
