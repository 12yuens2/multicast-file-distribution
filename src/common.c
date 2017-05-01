#include "header.h"

void print_header(header_packet header)
{
    printf("Header packet recieved\n");
    printf("filesize: %d\npack_size: %d\npacket_count: %d\nfilename: %s\nfile checksum: %d\n",
    header.filesize, header.packet_size, header.packet_count, header.filename, header.checksum);
}


int get_checksum(int fd, off_t start_offset, off_t stop_offset)
{
	off_t nbytes;
	uint32_t checksum;

	/* lseek to the starting offset */
	lseek(fd, start_offset, SEEK_SET);

	crc32(fd, &checksum, &nbytes, stop_offset);

	return checksum;
}


int higher(int a, int b)
{
	return (a > b) ? a : b;
}