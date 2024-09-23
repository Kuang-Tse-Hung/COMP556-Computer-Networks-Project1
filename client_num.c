#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <stdint.h>  // For uint64_t

/* Simple client, takes four parameters: 
   1. the server domain name,
   2. the server port number, 
   3. the size of each message, and 
   4. the number of message exchanges (count) */

int main(int argc, char** argv) {
    if (argc != 5) {
        printf("Usage: %s <hostname> <port> <size> <count>\n", argv[0]);
        return 1;
    }

    /* Client socket */
    int sock;

    /* Variables for identifying the server */
    struct addrinfo hints, *getaddrinfo_result;
    struct sockaddr_in sin;

    /* Server port number */
    unsigned short server_port = atoi(argv[2]);
    if(server_port > 18200 || server_port < 18000) {
        printf("Error: Server Port must be in range\n");
    }

    /* Message size and count */
    int size = atoi(argv[3]);
    int count = atoi(argv[4]);

    /* Validate size and count */
    if (size < 18 || size > 65535) {
        printf("Error: Size must be between 18 and 65535 bytes\n");
        return 1;
    }

    if (count < 1 || count > 10000) {
        printf("Error: Count must be between 1 and 10,000\n");
        return 1;
    }

    /* Buffer for sending and receiving messages */
    char *buffer = (char *)malloc(size);
    if (!buffer) {
        perror("Failed to allocate buffer");
        return 1;
    }

    /* Set up timestamps */
    struct timeval start, end;
    double total_rtt = 0;

    /* Set up hints for getaddrinfo */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* Use IPv4 */
    hints.ai_socktype = SOCK_STREAM; /* TCP */

    /* Convert server domain name to IP address */
    if (getaddrinfo(argv[1], NULL, &hints, &getaddrinfo_result) != 0) {
        perror("Failed to resolve hostname");
        free(buffer);
        return 1;
    }

    /* Create a socket */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Opening TCP socket failed");
        free(buffer);
        return 1;
    }

    /* Fill in the server's address */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr = ((struct sockaddr_in *)(getaddrinfo_result->ai_addr))->sin_addr;
    sin.sin_port = htons(server_port);

    freeaddrinfo(getaddrinfo_result);

    /* Connect to the server */
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        if (errno == EADDRINUSE) {
            fprintf(stderr, "Error: The port is already in use.\n");
        } else {
            perror("Connect to server failed");
        }
        close(sock);
        free(buffer);
        return 1;
    }

    /* Loop to perform 'count' ping-pong exchanges */
    for (int i = 0; i < count; i++) {
        /* Clear the buffer before sending */
        memset(buffer, 0, size);

        /* Add message size in the first 2 bytes (in network byte order) */
        unsigned short net_size = htons(size);
        memcpy(buffer, &net_size, sizeof(net_size));

        /* Get the current time before sending (start timestamp) */
        gettimeofday(&start, NULL);

        /* Insert the timestamp in the next 16 bytes (network byte order) */
        uint64_t sec = (uint64_t)start.tv_sec;
        uint64_t usec = (uint64_t)start.tv_usec;
        sec = htobe64(sec);  // Convert to network byte order (big-endian)
        usec = htobe64(usec); // Convert to network byte order (big-endian)
        memcpy(buffer + 2, &sec, sizeof(sec));
        memcpy(buffer + 10, &usec, sizeof(usec));

        uint64_t received_sec, received_usec;
        memcpy(&received_sec, buffer + 2, sizeof(received_sec));  // Extract sec from the received message
        memcpy(&received_usec, buffer + 10, sizeof(received_usec));  // Extract usec from the received message

        /* Convert from network byte order back to host byte order */
        received_sec = be64toh(received_sec);
        received_usec = be64toh(received_usec);

        /* Print the sent and received timestamps */
        printf("Sent Timestamp:    %ld sec, %ld usec\n", (long)start.tv_sec, (long)start.tv_usec);
        printf("Read Timestamp from pakage: %lu sec, %lu usec\n", (unsigned long)received_sec, (unsigned long)received_usec);

        /* Send the ping message */
        if (send(sock, buffer, size, 0) != size) {
            perror("Send failed");
            close(sock);
            free(buffer);
            return 1;
        }

        /* Now, receive the full pong message in a loop */
        int total_received = 0;
        while (total_received < size) {
            int bytes_received = recv(sock, buffer + total_received, size - total_received, 0);
            if (bytes_received < 0) {
                perror("Receive failed");
                close(sock);
                free(buffer);
                return 1;
            }
            if (bytes_received == 0) {
                // Connection closed by server
                printf("Connection closed by server during message reception.\n");
                close(sock);
                free(buffer);
                return 1;
            }
            total_received += bytes_received;
            printf("current received number at client: %d\n", total_received);
        }

        uint64_t pong_sec, pong_usec;
        memcpy(&pong_sec, buffer + 2, sizeof(pong_sec));  // Extract sec from the received message
        memcpy(&pong_usec, buffer + 10, sizeof(pong_usec));  // Extract usec from the received message

       
        pong_sec = be64toh(pong_sec);
        pong_usec = be64toh(pong_usec);

        printf("Read Timestamp from PONG pakage: %lu sec, %lu usec\n", (unsigned long)pong_sec, (unsigned long)pong_usec);

        /* Get the current time after receiving (end timestamp) */
        gettimeofday(&end, NULL);

        /* Calculate the round-trip time (RTT) in milliseconds */
        double rtt = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
        total_rtt += rtt;

        printf("Ping-Pong exchange %d: RTT = %.3f ms\n", i + 1, rtt);
    }

    /* Compute and print the average RTT */
    double avg_rtt = total_rtt / count;
    printf("Average RTT over %d exchanges: %.3f ms\n", count, avg_rtt);

    /* Clean up */
    close(sock);
    free(buffer);

    return 0;
}
