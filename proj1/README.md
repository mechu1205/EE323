# Project 1 : Socket Programming

## Specifications and How to Use

Please refer to the [Lab Material](https://docs.google.com/document/d/1Bo1ES8LHIY-Mrz2LeMPaJqwD16ratuvoX0zJliruE3U).


## Client Implementation

The client parses the user input and retrieves the values `host`, `port`, `op`, and `shift` via the `getopt` function.
If any of these values are missing from the input, the client will abort.       
Then the client enters a loop that only terminates when `EOF` is reached in the `stdin`. At the start of each loop, the client reads with a maximum of `BUFFER_SIZE` bytes from the `stdin`. `BUFFER_SIZE` is defined as the following:

    #define MSG_SIZE 1000000 //1M
    #define HEADER_SIZE 8
    #define BUFFER_SIZE (MSG_SIZE-HEADER_SIZE)

The data read from the `stdin` is copied to `buffer_in`, which is a buffer with `BUFFER_SIZE` bytes allocated to it. The length of the data is stored in a variable `read_size`.        
At this stage, the client creates the `msg` array, with  `read_size + HEADER_SIZE` byte size. `msg` is the array that will be sent to the client. Contents of the header, such as `op`, and the input message stored in `buffer_in` is copied to their appropriate positions in `msg`. Lastly, the checksum is calculated and filled into the `msg` header.       
The client now creates a socket and connects to the server at `host:port`. The client itself uses port 8888, a value defined at the start of `client.c` under the the alias `PORT`.     
If a successful connection is established, the contents of `msg` are transferred over to the server. After the trasmission is complete, `msg` is zeroed.     
The client enters a loop while waiting for the entire response to arrive. Note that the client is already aware of the proper response length, as it is equal to the length of the `msg` the client sent to the server.     
The response message is temporarily written to a `buffer_recv`, before being orderly copied to `msg`.       
The client verifies the checksum, and if it is correct, copies the content string of the `msg` to `stdout`.     
Finally, the client closes the socket, and, if `EOF` has not been reached at `stdin`, starts another iteration of the loop.

## Server Implementation

The server parses the user arguments and retrieves the `port` value to use.

## Checksum Creation and Verification

The checksums of messages are calculated and verified by a single `checksum` function, which is defined in both `client.c` and `server.c`.      
It is defined as

    uint16_t checksum(const char *buf, uint32_t size{
        // buf: pointer of the message
        // size: size of the message, in bytes
        
        /**
        If the checksum field in the message is zeroed, the correct checksum value will be returned.
        If the checksum field is not zeroed, it will be verified with the rest of the message. If the checksum value is correct, 0 will be returned; if the checksum is incorrect, a value greater than 0 will be returned.
        **/
    }
    
This function is a slight adjustment of the `checksum2` function defined on https://locklessinc.com/articles/tcp_checksum.