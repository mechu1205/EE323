# Project 2: Building a HTTP Proxy

## Specifications

Please refer to the [Lab Material](https://docs.google.com/document/d/1HUnVm4AT_EYpNMDOXbLMCp9QF0bHwspL2D_5O3HK6Ys).

## URL Parser

a function `parse_url` is implemented at the start of `proxy.c`.      

    int parse_url(char *url, char **host, char **path, int *port){
        // Parses string URL and saves the result to HOST, PATH, and PORT
        // e.g. URL = "www.example.com:80/index.html"
        // *host = "www.example.com", *path = "/index.html", *port = 80
        // return ptcl_found * 4 + port_found * 2 + path_found

`parse_url` parses `url` string and identifies the host, relative path, and port number. They are stored in `*host`, `*path`, and `port`.       
The function also identifies whether the protocol scheme, the port number, and the relative path were explicitly written in `url`, and formats the return value accordingly. This enables the caller of `parse_url` to mask the return value and see whether each of the above 3 features were included in the url.     
If the port number was not explicitly shown in the `url`, the value of `port` is defaulted to 80 (i.e. HTTP port number).       
Although protocol schemes such as https:// implies the port number that should be used, `parse_url` does not support such handling.     
If `url` does not contain the relative path, `path` is set to `"/"`. The difference that the result from `url = "example.com"` has over `url = "example.com/"` is that the return value, bitwise OR'ed with 1, equals 0 instead of 1.       

## Argument Parsing

Only one argument, the port number for the proxy to listen to, must be used when invoking the proxy.        
If the wrong number of arguments are given, or the given argument cannot be parsed as a non-zero integer, the following message will be printed out to `stdout`:

    fprintf (stdout, "Usage: %s <PORT>\n", argv[0]);

## Listening to and Accepting Connections

After the `listen` function returns, the proxy will enter an infinite loop, in which it `accept`s connection attempts from clients.     
When a connection is established successfully, it forks a new process to handle transmissions from the connected client.      

## Receiving Message from the Client

The proxy loops, `recv`'ing the message from the client and copies it orderly to a buffer called `msg`.      
The proxy simultaneously searches for the `"\r\n\r\n"` sequence (referred to as `eoh` in `proxy.c`), which marks the end of the message header, in `msg`.      
When `eoh` is found, the proxy exits the `recv` loop, and zeros all data after `eoh` in the `msg`. This is because the proxy only accepts GET requests from the client, and GET requests do not need any content after the message header.

## Parsing the Message from the Client

The proxy parses the `msg` and checks its validity.

* The proxy checks whether the `msg` starts with `"GET"`, followed by a whitespace.
* It parses the URL in the request line with the `parse_url` function explained above.
* It checks whether the "Host:" header line exists, and uniquely exists, in the `msg` header.
* It parses the URL in the "Host:" header line, and checks whether it is in the correct format, and whether it conflicts with the URL in the request line.

If any of the above conditions are not met, the proxy will return a 400 Bad Request to the client and close the connection.     
At this stage, the proxy also parses through the blacklist from `stdin` redirection, checking if the `host` requested by the client is included in the blacklist.     
If the `host` is found in the blacklist, the proxy will send a GET message to www.warning.or.kr instead of the requested `host`.

## Connecting and Sending Message to the Host

The proxy acquires the IPv4 addresses of the requested `host` by calling the `getaddrinfo` function.        
It iterates through the list of returned addresses, attempting connections. The iteration is terminated when a successful connection is established.        
If all of the addresses are iterated without a single successful connection being established, the proxy sends a 503 Service Unavailable to the client, and closes connection with it.
With a successful connection established with the `host`, the proxy sends a GET message, which is formatted as below:

    asprintf(&msg, "GET %s HTTP/1.0\r\n"
                    "Host: %s:%d\r\n"
                    "\r\n",
                    path_req, host_req, port_req);
    
    // e.g. "GET /index.html HTTP/1.0\r\n
    //      Host: www.foobar.com:80\r\n\r\n"

As shown, all header lines except "Host:" are discarded. Only the relative path is included in the request line, and the host and port are specified in the "Host" header line.

## Receiving and Relaying the Host Response

The proxy enters another loop while `recv`'ing the response from the `host`, while copying it orderly to the `msg` buffer.      
The proxy simultaneously searches for `eoh` (i.e. `"\r\n\r\n"`) in the `msg`, and when it is found, immediately searches for the "Content-Length:" header line in `msg`.        
From the "Content-Length" header line, the proxy parses the length of the remaining message after the header. <i>Absence of the "Content-Length" header line in the response message could lead to unintended behaviour.</i>        
At this point the proxy `send`s the content of the `msg` buffer to the client, and all data `recv`'ed from the host from this point on is relayed to the client without being copied to the `msg` buffer.       
After the entire message has been `recv`'ed and relayed to the client, the proxy closes down the connections with the host and the client.