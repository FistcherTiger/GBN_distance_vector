#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

#include "udp.h"


void initialize_udp()
{
  // Create Socket
  if ((self_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
  {
    printf("Error: Failed to Create Socket\n");
    exit(1);
  }

  // Init server and client address
  memset(&server_addr, 0, sizeof(server_addr));
  memset(&client_addr, 0, sizeof(client_addr));

  // Filling server information
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // Loopback Address      
  server_addr.sin_port = htons(self_port);
  client_addr.sin_family = AF_INET;
  client_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // Loopback Address      
  client_addr.sin_port = htons(peer_port);

  char ip_address[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client_addr.sin_addr), ip_address, INET_ADDRSTRLEN);
  int port_number = ntohs(client_addr.sin_port);

  printf("Client IP address: %s\n", ip_address);
  printf("Client port number: %d\n", port_number);
  
  // Bind the socket with the server address
  if (bind(self_socket, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    printf("Error: Failed to Bind Port\n");
    exit(1);
  }

  // Start Network Thread
  pthread_create(&network_thread, NULL, network_thread_f, NULL);
  
}

  //int send_buf_size = 2*window_size;
  
  //char send_buffer[send_buf_size];
  //strncpy(send_buffer, buffer, send_buf_size);
  //if(debug_mode == 1){printf("Send_buffer: %s\n",send_buffer);}

  //int window_pos = 0;


/* Send Message Using UDP Sockets */
void send_msg(int self_socket, char buffer, long send_sequence, struct sockaddr_in destaddr)
{

  char msg_buf[sizeof(long) + sizeof(char)];
  memcpy(msg_buf, &send_sequence, sizeof(long));
  memcpy(msg_buf + sizeof(long), &buffer, sizeof(char));

  if(debug_mode==1){printf("To send: ");
  for (int i = 0; i < sizeof(msg_buf); i++) 
  {
    printf("%02X ", (unsigned char)msg_buf[i]);
  }
  printf("\n");}

  char ip_address[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client_addr.sin_addr), ip_address, INET_ADDRSTRLEN);
  
  //int port_number = ntohs(client_addr.sin_port);
  //printf("Client IP address: %s\n", ip_address);
  //printf("Client port number: %d\n", port_number);

  sendto(self_socket, msg_buf, sizeof(long) + sizeof(char), 0, (const struct sockaddr *)&client_addr, sizeof(client_addr));

  return;
}
