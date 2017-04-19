#ifndef __CS3102__NET_PRACTICAL_
#define __CS3102__NET_PRACTICAL_

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <time.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <linux/limits.h>

#include "extern.h"

#define MULTICAST_PORT 18238
#define MULTICAST_GROUP "233.0.133.0"
#define MAX_CONNECTIONS 100

#define WINDOW_SIZE 256
#define BUFFER_SIZE 8192
#define MAX_FILENAME 256
#define IP_LENGTH 16

/* Types of control messages */
#define WINDONE_MSG 91
#define RESEND_MSG 101
#define NACK_MSG 111
#define ACK_MSG 121

/* Macros */
#define MAX(x,y) (((x)>(y))?(x):(y))
#define MIN(x,y) (((x)<(y))?(x):(y))
#define WINDOW_OFFSET(window_number) ((window_number)*(WINDOW_SIZE)*(BUFFER_SIZE))
#define WRITE_LOCATION(packet_number) ((packet_number)*(BUFFER_SIZE))

/*
 * UDP packets 
 */
typedef struct data
{
    int packet_number;
    int packet_length;
    int window_number;
    char body[BUFFER_SIZE + 1];

} data_packet;

/* 
 * TCP packets 
 */
typedef struct header
{
    int filesize;
    int packet_size;
    int packet_count;
    int checksum;
    char filename[MAX_FILENAME];

} header_packet;

typedef struct control
{
    int type;
    int window_number;
    off_t window_offset;
    int checksum;

} control_packet;

typedef struct nack
{
    int missing_packet_count;
    int missing_packets[WINDOW_SIZE];

} nack_packet;



/* Helper functions used by both client and server */

/**
  * Prints the data of a header_packet.
  */
void print_header(header_packet header);


/**
  * Uses the function in crc32.c to calculate the checksum of the given file descriptor.
  * To checksum the full file, give a start_offset of 0 and stop_offset of the end of the file.
  * Otherwise the exact chunk of the file to be checked can be specified by the start and stop offset.
  */
int get_checksum(int fd, off_t start_offset, off_t stop_offset);


/**
  * Helper function that returns the higher of the two given integers.
  */
int higher(int a, int b);


#endif
