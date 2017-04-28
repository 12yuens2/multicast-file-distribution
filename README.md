# Multicast File Distribution
This is a multicast file distribution protocol with implementation written in C. I have only tested this using ethernet multicast on a local network.

## Compiling
To compile, run `make` in the src directory.


## Running the Server
Usage: 
`./server [num_clients] [filepath] [port]`

The server will wait until the number of clients connected is equal to num_clients before it start sending the file.
The server will display its network interfaces so clients can see what its ip address is.
Header information of the file specified at the given filepath will be printed when the server starts sending the file.


## Running the Client
Usage: 
`./client [server_ip] [destination_path] [port]`

The client will get a file from the server specified by the server_ip and copy it to the directory specified by destination_path.


## Notes
The code in the two files crc32.c and extern.h are taken from http://web.mit.edu/freebsd/head/usr.bin/cksum/
