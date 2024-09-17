#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

/* ping pong client, takes four parameters, the server domain name,
   the server port number, data size and count */

int main(int argc, char **argv)
{
  /* check arg numbers */
  if (argc != 5)
  {
    printf("Please input 4 parameters in this order: hostname, port, size, count.\n");
    return 1;
  }
  /* our client socket */
  int sock;

  /* variables for identifying the server */
  unsigned int server_addr;
  struct sockaddr_in sin;
  struct addrinfo *getaddrinfo_result, hints;

  /* convert server domain name to IP address */
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET; /* indicates we want IPv4 */

  if (getaddrinfo(argv[1], NULL, &hints, &getaddrinfo_result) == 0)
  {
    server_addr = (unsigned int)((struct sockaddr_in *)(getaddrinfo_result->ai_addr))->sin_addr.s_addr;
    freeaddrinfo(getaddrinfo_result);
  }

  /* server port number */
  unsigned short server_port = atoi(argv[2]);

  int size_int = atoi(argv[3]);
  int count = atoi(argv[4]);
  int num;

  if (size_int < 18 || size_int > 65535)
  {
    printf("size should be in range [18, 65535]\n");
    return 1;
  }
  if (count < 1 || count > 10000)
  {
    printf("count should be in range [1, 10000]\n");
    return 1;
  }
  /* allocate a memory buffer in the heap */
  /* putting a buffer on the stack like:

         char buffer[500];

     leaves the potential for
     buffer overflow vulnerability */

  uint16_t size = (uint16_t)size_int;
  uint8_t *msg = (uint8_t*)malloc(size);
  struct timeval start,end;
  double total_latency = 0;

  /* create a socket */
  if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
  {
    perror("opening TCP socket");
    abort();
  }

  /* fill in the server's address */
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = server_addr;
  sin.sin_port = htons(server_port);

  /* connect to the server */
  if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
  {
    perror("connect to server failed");
    abort();
  }

  for (int i = 0; i < count; i++)
  {
    gettimeofday(&start,NULL);
    // copy the messages to variable msg
    // first 2 bytes: size
    memcpy(msg, &size, sizeof(uint16_t));
    // then 16 bytes for timestamp
    memcpy(&msg[2], &start.tv_sec, sizeof(start.tv_sec));    
    memcpy(&msg[10], &start.tv_usec, sizeof(start.tv_usec));
    // let default for the rest data
    
    if (send(sock, msg, size, 0)!=size){
      perror("Send failed");
      close(sock);
      free(msg);
      return 1;
    }

    if (recv(sock, msg, size, 0) != size) {
      perror("Receive failed");
      close(sock);
      free(msg);
      return 1;
    }

    gettimeofday(&end,NULL);
    // get start timestamp from pong msg
    memcpy(&start.tv_sec, &msg[2], sizeof(start.tv_sec));    
    memcpy(&start.tv_usec, &msg[10], sizeof(start.tv_usec)); 

    // in milliseconds
    double latency = (end.tv_sec-start.tv_sec) * 1000.0 + (end.tv_usec-start.tv_sec) / 1000.0;
    total_latency+=latency;
  }

  double avg_latency = total_latency / count;
  printf("Average latency: %.3f ms\n", avg_latency);

  return 0;
}
