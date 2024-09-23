#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/time.h>

/**************************************************/
/* a few simple linked list functions             */
/**************************************************/

struct node {
    int socket;
    struct sockaddr_in client_addr;
    int pending_data;
    struct node *next;
};

/* remove the data structure associated with a connected socket */
void dump(struct node *head, int socket) {
    struct node *current, *temp;
    current = head;

    while (current->next) {
        if (current->next->socket == socket) {
            temp = current->next;
            current->next = temp->next;
            free(temp);
            return;
        } else {
            current = current->next;
        }
    }
}

/* create the data structure associated with a connected socket */
void add(struct node *head, int socket, struct sockaddr_in addr) {
    struct node *new_node;
    new_node = (struct node *)malloc(sizeof(struct node));
    new_node->socket = socket;
    new_node->client_addr = addr;
    new_node->pending_data = 0;
    new_node->next = head->next;
    head->next = new_node;
}

/*****************************************/
/* main program                          */
/*****************************************/

int main(int argc, char **argv) {

    int sock, new_sock, max;
    int optval = 1;

    struct sockaddr_in sin, addr;
    unsigned short server_port = atoi(argv[1]);

    socklen_t addr_len = sizeof(struct sockaddr_in);

    int BACKLOG = 5;

    fd_set read_set, write_set;
    struct timeval time_out;
    int select_retval;

    struct node head;
    struct node *current, *next;

    /* Allocate buffer size dynamically */
    int BUF_LEN = 65535; /* Max message size as per project requirement */
    char *buf = (char *)malloc(BUF_LEN);
    if (!buf) {
        perror("failed to allocate buffer");
        abort();
    }

    head.socket = -1;
    head.next = 0;

    /* Create socket */
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("opening TCP socket");
        abort();
    }

    /* Set socket options */
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setting TCP socket option");
        abort();
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(server_port);

    /* Bind socket */
    if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("binding socket to address");
        abort();
    }

    /* Listen for connections */
    if (listen(sock, BACKLOG) < 0) {
        perror("listen on socket failed");
        abort();
    }

    /* Main server loop */
    while (1) {

        FD_ZERO(&read_set);
        FD_ZERO(&write_set);

        FD_SET(sock, &read_set);
        max = sock;

        for (current = head.next; current; current = current->next) {
            FD_SET(current->socket, &read_set);
            if (current->pending_data) {
                FD_SET(current->socket, &write_set);
            }
            if (current->socket > max) {
                max = current->socket;
            }
        }

        time_out.tv_usec = 100000;
        time_out.tv_sec = 0;

        select_retval = select(max + 1, &read_set, &write_set, NULL, &time_out);
        if (select_retval < 0) {
            perror("select failed");
            abort();
        }

        if (select_retval == 0) {
            continue;
        }

        if (select_retval > 0) {
            /* Accept new connection */
            if (FD_ISSET(sock, &read_set)) {
                new_sock = accept(sock, (struct sockaddr *)&addr, &addr_len);

                if (new_sock < 0) {
                    perror("error accepting connection");
                    abort();
                }

                /* Set the socket to non-blocking mode */
                if (fcntl(new_sock, F_SETFL, O_NONBLOCK) < 0) {
                    perror("making socket non-blocking");
                    abort();
                }

                printf("Accepted connection. Client IP address is: %s\n", inet_ntoa(addr.sin_addr));
                add(&head, new_sock, addr);
            }

            /* Handle data from existing connections */
            for (current = head.next; current; current = next) {
                next = current->next;

                if (FD_ISSET(current->socket, &read_set)) {
                    /* Step 1: Receive the entire message (including the size) */
                    int total_received = 0;
                    int total_size = 0;
                    int recv_count = 0;

                    /* First, receive the first part of the message (including size and part of the payload) */
                    recv_count = recv(current->socket, buf, BUF_LEN, 0);
                    if (recv_count <= 0) {
                        if (recv_count == 0) {
                            printf("Client closed connection. Client IP address is: %s\n", inet_ntoa(current->client_addr.sin_addr));
                        } else {
                            perror("Error receiving message from client");
                        }
                        close(current->socket);
                        dump(&head, current->socket);
                        continue;
                    }
                    total_received += recv_count;

                    /* Step 2: Extract the size from the buffer (without removing it) */
                    unsigned short expected_size = 0;
                    memcpy(&expected_size, buf, sizeof(unsigned short));
                    expected_size = ntohs(expected_size);
                    printf("Expected size (including size bytes): %d\n", expected_size);

                    total_size = expected_size;

                    /* Step 3: Continue receiving the remaining part of the message, if necessary */
                    while (total_received < total_size) {
                        recv_count = recv(current->socket, buf + total_received, total_size - total_received, 0);
                        if (recv_count <= 0) {
                            if (recv_count == 0) {
                                printf("Client closed connection during message reception. IP: %s\n", inet_ntoa(current->client_addr.sin_addr));
                            } else {
                                perror("Error receiving full message");
                            }
                            close(current->socket);
                            dump(&head, current->socket);
                            break;
                        }
                        total_received += recv_count;
                    }

                    /* Step 4: Process the message (including extracting the timestamp) */
                    if (total_received == total_size) {
                        uint64_t received_sec, received_usec;
                        memcpy(&received_sec, buf + 2, sizeof(received_sec));  // Start from byte 2
                        memcpy(&received_usec, buf + 10, sizeof(received_usec));  // Start from byte 10

                        received_sec = be64toh(received_sec);
                        received_usec = be64toh(received_usec);

                        printf("Received Timestamp: %lu sec, %lu usec\n", (unsigned long)received_sec, (unsigned long)received_usec);

                        /* Step 5: Echo the message back to the client */
                        int total_sent = 0;
                        while (total_sent < total_size) {
                            int sent = send(current->socket, buf + total_sent, total_size - total_sent, 0);
                            if (sent == -1) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    continue;
                                } else {
                                    perror("Error sending pong");
                                    break;
                                }
                            }
                            total_sent += sent;
                        }

                        printf("Sent %d bytes back to client from %s\n", total_sent, inet_ntoa(current->client_addr.sin_addr));
                    }
                }
            }
        }
    }

    free(buf); /* Free the allocated buffer */
    return 0;
}
