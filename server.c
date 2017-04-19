#include "header.h"

struct sockaddr_in m_address, tcp_address;
struct stat file_stat;
int fd, m_sd, tcp_sd, client_sd[MAX_CONNECTIONS];
int highest_sd = 0;

void setup_multicast_socket()
{
    /* Create multicast UDP socket */
    if ((m_sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("UDP socket could not be created\n");
        exit(-1);
    }

    /* Create address for multicast UDP socket */
    memset(&m_address, 0, sizeof(m_address));
    m_address.sin_family = AF_INET;
    m_address.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
    m_address.sin_port = MULTICAST_PORT;
}

void setup_server_tcp_socket(int port) 
{
    /* Get our own ip with hostname -i */
    FILE* p = popen("hostname -i", "r");
    if (p == NULL)
    {
        perror("Failed to run command 'hostname -i' to get ip address");
        exit(-1);
    }

    char tcp_ip[IP_LENGTH];
    fgets(tcp_ip, sizeof(tcp_ip)-1, p);

    /* Create TCP control socket */
    if ((tcp_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("TCP socket could not be created");
        exit(-1);
    }

    /* Create address for TCP connection */
    memset(&tcp_address, 0, sizeof(tcp_address));
    tcp_address.sin_family = AF_INET;
    tcp_address.sin_addr.s_addr = inet_addr(tcp_ip);
    tcp_address.sin_port = port;

    /* Bind address to socket */
    if (bind(tcp_sd, (struct sockaddr*) &tcp_address, sizeof(tcp_address)) < 0)
    {
        perror("Failed to bind to tcp socket");
        exit(-1);
    }

    /* Mark socket to listen for connections */
    if (listen(tcp_sd, MAX_CONNECTIONS) < 0)
    {
        perror("Failed to listen on tcp socket");
        exit(-1);
    }

    int yes = 1;
    setsockopt(tcp_sd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &yes, sizeof(int));

    highest_sd = higher(tcp_sd, highest_sd);
}

void print_ips() 
{    
    struct ifaddrs* addrs;
    getifaddrs(&addrs);

    struct ifaddrs* current = addrs;

    /* Loop through all network interfaces */
    while (current != NULL) 
    {
        if (current->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in* addr = (struct sockaddr_in*) current->ifa_addr;
            printf("%s: %s\n", current->ifa_name, inet_ntoa(addr->sin_addr));
        }

        current = current->ifa_next;
    }

    freeifaddrs(addrs);
}

int open_file(char* filepath)
{
    if ((fd = open(filepath, O_RDONLY)) < 0)
    {
        perror("Error opening file");
        exit(-1);
    }

    if (stat(filepath, &(file_stat)) < 0)
    {
        perror("Error stating file");
        exit(-1);
    }

    /* Do a checksum on the whole file */
    off_t stop_offset = lseek(fd, 0, SEEK_END);
    int checksum = get_checksum(fd, 0, stop_offset);

    /* Reset file offset to beginning of file */
    lseek(fd, 0, SEEK_SET);
    
    return checksum;
}

/**
  * Sends a message of 'len' bytes from 'buf' to the descriptor 'sd'.
  * For TCP sockets, 'address' field is ignored.
  * For UDP sockets, 'address' field is required as the target address.
  */
void send_msg(const void* buf, size_t len, int sd, struct sockaddr_in address)
{
    if (sendto(sd, buf, len, MSG_CONFIRM, (struct sockaddr*) &address, sizeof(address)) < 0)
    {
        perror("Failed to send header message");
        exit(-1);
    }
}

/**
  * Sends a control packet to all connected TCP clients, specified by client_sd[].
  * 'conns' specifies the number of clients.
  * 'type' specifies the type of control_packet.
  */
void send_to_all(int conns, int window_number, int type)
{
    control_packet ctrl_packet;
    if (type == WINDONE_MSG)
    {
        off_t stop_offset = lseek(fd, 0, SEEK_CUR);
        int checksum = get_checksum(fd, WINDOW_OFFSET(window_number), stop_offset);

        ctrl_packet.type = WINDONE_MSG;
        ctrl_packet.checksum = checksum;
        ctrl_packet.window_number = window_number;
        ctrl_packet.window_offset = stop_offset;
    }
    else 
    {
        ctrl_packet.type = type;
    }

    for (int i = 0; i<conns; i++)
    {
        send_msg(&ctrl_packet, sizeof(control_packet), client_sd[i], tcp_address);
    }
}

/**
  * Creates a header_packet and stores it in address pointed to by 'header'.
  */
void create_header_packet(header_packet* header, int filesize, int packet_size, int checksum, char filename[])
{
    header->filesize = filesize;
    header->packet_size = packet_size;
    header->packet_count = (filesize / packet_size ) + 1;
    header->checksum = checksum;
    strcpy(header->filename, filename);
}

/**
  * Creates a data_packet and stores it in address pointed to by 'packet'.
  * 'packet_length' bytes from 'buf' are written to the body of the data_packet.
  */
void create_data_packet(data_packet* packet, void* buf, int packet_number, int packet_length, int window_number)
{
    memset(packet, 0, sizeof(data_packet));
    memcpy(packet->body, buf, packet_length);
    packet->packet_number = packet_number;
    packet->packet_length = packet_length;
    packet->window_number = window_number;
}

/**
  * Finds the missing packet specified by the packet_number and window_number
  * and sends the packet through the multicast socket m_sd.
  */
void resend_missing_packet(int packet_number, int window_number)
{
    data_packet packet;
    int nbytes;

    char buffer[BUFFER_SIZE];
    memset(&buffer, 0, BUFFER_SIZE+1);

    /* lseek() to find the starting offset of the missing packet */
    lseek(fd, WINDOW_OFFSET(window_number) + WRITE_LOCATION(packet_number), SEEK_SET);
    nbytes = read(fd, buffer, BUFFER_SIZE);

    create_data_packet(&packet, buffer, packet_number, nbytes, window_number);

    send_msg(&packet, sizeof(data_packet), m_sd, m_address);
}

/**
  * Handler for nack_packets. 
  * Reads a nack_packet from 'sd' and resends all packets listed in nack_packet.missing_packets[].
  */
void handleNackMessage(int sd, int window_number)
{
    nack_packet nack;
    int nbytes;
    if ((nbytes = recv(sd, &nack, sizeof(nack_packet), MSG_WAITALL)) < 0)
    {
        perror("Failed to recv from client tcp connection");
        exit(-1);
    }

    for (int i = 0; i<nack.missing_packet_count; i++)
    {
        resend_missing_packet(nack.missing_packets[i], window_number);
    }

}

/**
  * Handler for all client TCP messages.
  * Message types are specified in "header.h"
  */
int handleClientMessage(int sd, int window_number, int acks, int* resend)
{
    nack_packet nack;

    control_packet msg;
    int nbytes;
    if ((nbytes = recv(sd, &msg, sizeof(control_packet), MSG_WAITALL)) < 0)
    {
        perror("Failed to recv from client tcp connection");
        exit(-1);
    }

    switch(msg.type)
    {
        case ACK_MSG:
            return acks + 1;

        case RESEND_MSG:
            printf("Resending window %d\n", window_number);
            *resend = 1;
            return acks + 1;

        case NACK_MSG:
            handleNackMessage(sd, window_number);
        
        default:
            return acks;
    }

}

/**
  * Accept an incoming client connection and send them the header for the file.
  * Returns the socket descriptor for this client connection.
  */
int accept_client_connection(header_packet header, int conns)
{
    int addrlen = sizeof(tcp_address);
    if ((client_sd[conns] = accept(tcp_sd, (struct sockaddr*) &tcp_address, &addrlen)) < 0)
    {
        perror("Failed to accept new connection\n");
        exit(-1);
    }
    highest_sd = higher(client_sd[conns], highest_sd);

    send_msg(&header, sizeof(header), client_sd[conns], tcp_address);

    return client_sd[conns];
}



int main(int argc, char const *argv[])
{
    int num_clients = atoi(argv[1]);
    char file_to_send[PATH_MAX + MAX_FILENAME];
    strcpy(file_to_send, argv[2]);
    int port = atoi(argv[3]);
    print_ips(); 

    setup_multicast_socket();
    setup_server_tcp_socket(port);

    int checksum = open_file(file_to_send);

    header_packet header;
    create_header_packet(&header, file_stat.st_size, BUFFER_SIZE, checksum, basename(file_to_send));

    char buffer[BUFFER_SIZE];
    memset(&buffer, 0, BUFFER_SIZE+1);

    /* Structs for timing */
    struct timespec start_time, stop_time;

    /* Create socket descriptor lists for select() */
    fd_set readfds, master;
    FD_ZERO(&master);

    /* Accept connections and add the client socket to master */
    int connections = 0;
    while(connections < num_clients)
    {
        int sd = accept_client_connection(header, connections);
        FD_SET(sd, &master);
        connections++;
        if (connections == 1)
        {
            /* Start timing how long it takes to transfer the file to clients */
            clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);
        }
    }

    print_header(header);

    int nbytes = 1, window_number = 0;
    while (nbytes > 0)
    {
        int sequence_number = 0;
        lseek(fd, window_number*WINDOW_SIZE*BUFFER_SIZE, SEEK_SET);

        /* Send a window */
        while (sequence_number < WINDOW_SIZE && (nbytes = read(fd, buffer, BUFFER_SIZE)) > 0)
        {
            data_packet packet;
            create_data_packet(&packet, buffer, sequence_number, nbytes, window_number);

            send_msg(&packet, sizeof(data_packet), m_sd, m_address);

            /* Reset data buffer for next read */
            memset(&buffer, 0, BUFFER_SIZE + 1);

            sequence_number++;
        }

        /* Tell all clients the window has finished */
        send_to_all(connections, window_number, WINDONE_MSG);

        /* Get control_packets and nacks from clients */
        int acks = 0, resend = 0;
        while (acks < connections)
        {
            readfds = master;
            if (select(highest_sd+1, &readfds, NULL, NULL, NULL) < 0)
            {
                perror("Failed on selecting socket");
                exit(-1);
            }

            for (int i = 0; i<connections; i++)
            {
                if (FD_ISSET(client_sd[i], &readfds))
                {
                    acks = handleClientMessage(client_sd[i], window_number, acks, &resend);
                }
            }
        }
        printf("Window %d finished transmitting, sent %d packets \n", window_number, sequence_number);

        /* Tell clients if we are moving to the next window or resending a window */
        if (resend)
        {
            send_to_all(connections, 0, RESEND_MSG);
            nbytes = 1;
        }
        else
        {
            send_to_all(connections, 0, ACK_MSG);
            window_number++;
        }
    }

    /* Stop the timer as file transfer is complete */
    clock_gettime(CLOCK_MONOTONIC_RAW, &stop_time);

    uint64_t time_taken = (stop_time.tv_sec - start_time.tv_sec) * 1000 + (stop_time.tv_nsec - start_time.tv_nsec) / 1000000;
    printf("Time taken: %dms\n", time_taken);

    /* Clean up */
    for (int i = 0; i<connections; i++)
    {
        close(client_sd[i]);
    }
    close(tcp_sd);
    close(m_sd);
    close(fd);
    return 0;
}
