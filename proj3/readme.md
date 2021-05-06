## transport_init()

transport_init() handles all socket features up till connection establishment.  
When the 3-way handshake is complete and `connection_state` is set to `CSTATE_ESTB`, the function breaks out of the loop, unblocks the application, and calls `control_loop()`.

## control_loop()

`control_loop()` iterates through a loop until connection is closed. At the start of each loop, the function `stcp_wait_for_event()` is called. Note that it is called with `NULL` for the `abstime` argument, which means that no timeout is implemented. Packet transmissions (including SFTP) and state transitions are done inside this loop.

## data send-buffer control

When there is data to be sent, it is written to `buf_send`. The capacity of `buf_send` is decided as the smaller value of `cwnd` and 3072, and will eventually converge to 3072.

    buf_send (||| indicates data already sent but yet to be ack'ed)
     ---------------
    |    ||||||     |
     ---------------
    |----| buf_send_start
    |---------| buf_send_end
    |---------------| buf_send_cap
    
When the app requests that data be sent, the data is copied to `buf_send`, up to `buf_send_cap - buf_send_end` bytes, and then immediately sent to the connected peer in packets (each of which contains at most 536 bytes of data, excluding the header).      
Arrival of packets with increased ACK numbers mean that the peer has safely received additional data. The sender needs not keep track of data that is ack'ed by the peer. Therefore, on such cases, `buf_send_start` is increased.       
The data between `buf_send_start` and `buf_send_end` is the data that has been sent, but yet to be ack'ed. If `buf_send_end` is too high, the socket can only receive small (even zero) amounts of data from the app with each `write()` call. Therefore, when `buf_send_start` becomes higher than half of `buf_send_cap`, all data inside the buffer are shifted to the front of `buf_send`, `buf_send_end` is decreased by `buf_send_start`, and `buf_send_start` is set to zero.
