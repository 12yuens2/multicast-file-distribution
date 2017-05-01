#include "header.h"

struct sockaddr_in m_address, tcp_address;
struct ip_mreq mreq;

int m_sd, tcp_sd; /* Socket descriptors */

/* Keeps track of the largest socket descriptor for select() */
int highest_sd = 0; 


void setup_client_multicast_socket()
{
    u_int yes = 1;

    /* Create multicast socket */
    if ((m_sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Failed to create client UDP socket");
        exit(-1);
    }

    highest_sd = higher(m_sd, highest_sd);

    /* Set socket options to reuse same port */
    if (setsockopt(m_sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
    {
        perror("Failed to reuse port address");
        exit(-1);
    }

    /* Create address to bind to socket */
    memset(&m_address, 0, sizeof(m_address));
    m_address.sin_family = AF_INET;
    m_address.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
    m_address.sin_port = MULTICAST_PORT;

    /* Binding address to socket */
    if (bind(m_sd, (struct sockaddr*)&m_address, sizeof(m_address)) < 0)
    {
        perror("Failed to bind socket to address");
        exit(-1);
    }

    /* Set multicast group */
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(m_sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        perror("Failed to add to multicast group\n");
        exit(-1);
    }
}

void setup_client_tcp_socket(char* ip, int port)
{
    /* Create TCP socket for control messages */
    if ((tcp_sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("Failed to create client TCP socket");
        exit(-1);
    }

    highest_sd = higher(tcp_sd, highest_sd);

    /* Create address to bind to socket */
    memset(&tcp_address, 0, sizeof(tcp_address));
    tcp_address.sin_family = AF_INET;
    tcp_address.sin_addr.s_addr = inet_addr(ip);
    tcp_address.sin_port = port;

    /* Connect to server's TCP socket */
    if (connect(tcp_sd, (struct sockaddr*) &tcp_address, sizeof(tcp_address)) < 0)
    {
        perror("Failed to connect to tcp server\n");
        exit(-1);
    }

}

/**
  * This function reads 'len' bytes from 'sd' and 'address'.
  * The result is then stored in 'buf'.
  */
void get_msg(void* buf, int len, int sd, struct sockaddr_in address)
{
    socklen_t addrlen;
    if (recvfrom(sd, buf, len, 0, (struct sockaddr*) &address, &addrlen) < 0)
    {
        perror("Error on recvfrom\n");
        exit(-1);
    }
}

/** 
  * Sends a message of 'size' bytes pointed at by 'buf' to the descriptor 'sd'.
  * This is only suitable for TCP sockets that are already connected as there is no specified address.
  */
void send_msg(int sd, const void* buf, size_t size)
{
    unsigned int nbytes = 0;
    if ((nbytes = send(sd, buf, size, 0)) != size)
    {
        printf("Sent a different number of bytes than expected\n");
    }
}

/**
  * Sends a control_packet with the given type to tcp_sd.
  * Given types of messages are as specified in "header.h".
  */
void send_control(int type)
{
    control_packet ctrl;
    ctrl.type = type;

    send_msg(tcp_sd, &ctrl, sizeof(control_packet));
}

/**
  * Creates a nack_packet with its missing_packets based on 0s in the missing_packet_map.
  * Returns a pointer to the allocated nack_packet.
  */
nack_packet* populate_nack(int* missing_packet_map)
{
    nack_packet* nack = calloc(1, sizeof(nack_packet));
    nack->missing_packet_count = 0;
    for (int i = 0; i<WINDOW_SIZE; i++)
    {
        if (missing_packet_map[i] == 0)
        {
            nack->missing_packets[nack->missing_packet_count] = i;
            nack->missing_packet_count++;
        }
    }

    return nack;
}

int write_to_file(int fd, data_packet packet)
{
    lseek(fd, WINDOW_OFFSET(packet.window_number) + WRITE_LOCATION(packet.packet_number), SEEK_SET);
    write(fd, packet.body, packet.packet_length);

    return fd;
}

int main(int argc, char const *argv[])
{
    (void)argc;

    char server_ip[IP_LENGTH];
    strcpy(server_ip, argv[1]);

    char file_dst_path[PATH_MAX];
    strcpy(file_dst_path, argv[2]);

    int port = atoi(argv[3]);

    setup_client_multicast_socket();
    setup_client_tcp_socket(server_ip, port);

    /* Get header_packet */
    header_packet header;
    get_msg(&header, sizeof(header_packet), tcp_sd, tcp_address);

    print_header(header);

    char filepath[PATH_MAX + MAX_FILENAME];
    strcpy(filepath, file_dst_path);
    strcat(filepath, header.filename);

    /* Open the file to write to */
    int fd = open(filepath, O_RDWR | O_TRUNC | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);

    /* Total packets and windows in this transfer */
    int total_packets = header.packet_count;
    int total_windows = header.filesize / (WINDOW_SIZE*BUFFER_SIZE);

    /* Map for checking which packets are missing */
    int missing_packet_map[WINDOW_SIZE];

    /* fd_sets for select */
    fd_set readfds, multicastfds, master;

    /* master contains both the TCP and multicast socket descriptors */
    FD_ZERO(&master);
    FD_SET(m_sd, &master);
    FD_SET(tcp_sd, &master);

    /* multicastfds only contains the multicast socket descriptor */
    FD_ZERO(&multicastfds);
    FD_SET(m_sd, &multicastfds);

    data_packet packet;
    int packets_received = 0, window_number = 0;
    while(window_number <= total_windows || packets_received < total_packets)
    {
        int packets_left = total_packets - packets_received;

        /* Reset the missing_packet_map */
        memset(missing_packet_map, 0, sizeof(missing_packet_map)); 

        /*
         * Set missing_packet_map if the next window is smaller than WINDOW_SIZE
         * This only happens on the last window 
         */
        if (packets_left < WINDOW_SIZE)
        {
            for (int i = packets_left; i<WINDOW_SIZE; i++)
            {
                missing_packet_map[i] = 1;
            }
        }

        /*
         * Get packets for the window.
         * End early if we receive TCP activity as the server has finished.
         */
        int packets_missing = 0, window_packets = 0, current_packets = 0;
        while (window_packets < WINDOW_SIZE)
        {
            /* select() over the socket descriptors in master with no timeout */
            readfds = master;
            select(highest_sd+1, &readfds, NULL, NULL, NULL);

            /* Got message from UDP socket so deal with it as a data_packet */
            if (FD_ISSET(m_sd, &readfds))
            {
                get_msg(&packet, sizeof(packet), m_sd, m_address);

                if (packet.window_number == window_number)
                {
                    lseek(fd, WINDOW_OFFSET(packet.window_number) + WRITE_LOCATION(packet.packet_number), SEEK_SET);
                    write(fd, packet.body, packet.packet_length);

                    missing_packet_map[packet.packet_number] = 1;

                    current_packets++;
                    packets_received++; 
                    window_packets++;
                }
                
            }

            /* 
             * Got message from TCP socket so the server must have finished sending all data_packets in this
             * window before we received them all. 
             */
            else if (FD_ISSET(tcp_sd, &readfds))
            {
                packets_missing += MIN(packets_left - window_packets, WINDOW_SIZE - window_packets);
                break;
            }
        }

        printf("Finished window %d, packets missing: %d, packets recieved: %d out of %d\n", window_number, packets_missing, current_packets, MIN(packets_left, WINDOW_SIZE));
        printf("Overall process %d out of %d\n", packets_received, total_packets);

        /* Block until we receive the control_packet from the server indicating the end of the window */
        control_packet ctrl;
        get_msg(&ctrl, sizeof(control_packet), tcp_sd, tcp_address);

        /* Sync our window number with the server */
        window_number = ctrl.window_number;

        /* Create and send a nack_packet for any packets we missed in this window */
        nack_packet* nack = populate_nack(missing_packet_map);
        send_control(NACK_MSG);
        send_msg(tcp_sd, nack, sizeof(nack_packet));

        /* Timeout for receiving packets we nacked before sending another nack */
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        while(packets_missing > 0)
        {
            /* Wait for data on multicast socket with select() */
            readfds = multicastfds;
            select(highest_sd+1, &readfds, NULL, NULL, &timeout);

            if (FD_ISSET(m_sd, &readfds))
            {
                get_msg(&packet, sizeof(packet), m_sd, m_address);

                if (missing_packet_map[packet.packet_number] == 0)
                {
                    lseek(fd, WINDOW_OFFSET(packet.window_number) + WRITE_LOCATION(packet.packet_number), SEEK_SET);
                    write(fd, packet.body, packet.packet_length);

                    missing_packet_map[packet.packet_number] = 1;
                    packets_missing--;
                    packets_received++;
                    window_packets++;
                }
            }

            /* Send another nack as timeout as passed */
            else
            {
                free(nack);
                nack = populate_nack(missing_packet_map);
                send_control(NACK_MSG);
                send_msg(tcp_sd, nack, sizeof(nack_packet));
            }
        }
        free(nack);

        int checksum = get_checksum(fd, WINDOW_OFFSET(ctrl.window_number), ctrl.window_offset);
        printf("Control checksum: %d\tWindow checksum: %d\n\n", ctrl.checksum, checksum);

        /* Send control_packet back to server */
        (ctrl.checksum != checksum) ? send_control(RESEND_MSG) : send_control(ACK_MSG);

        /* Get final control_packet from server */
        control_packet server_ack;
        get_msg(&server_ack, sizeof(control_packet), tcp_sd, tcp_address);
        (server_ack.type == RESEND_MSG) ? packets_received -= MIN(WINDOW_SIZE, window_packets) : window_number++;
    }

    /* End of file final checksum */
    off_t stop_offset = lseek(fd, 0, SEEK_END);
    int checksum = get_checksum(fd, 0, stop_offset);

    printf("Header checksum: %d\nFinal checksum: %d\n", header.checksum, checksum);

    /* Clean up */
    close(tcp_sd);
    close(fd);
    printf("Done.\n");

    return 0;
}
