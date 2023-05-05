#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <regex.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

/* Custom Headers */
#include "dvnode.h"
#include "vector.h"

/* Stored Data */
struct distance_vector *head_dv = NULL;                        // Header distance vector
struct edge_table *head_et = NULL;                             // Header routing table


pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

int is_initial_sent = 0;                                 // Flagging whether the program have sent its distance vectors to anyone, 0 not yet, 1 sent, 2 last
int flag_send = 0;                                       // Flagging whether the distance vector should be sent out

/* Main Thread For Processing Commands */
int main(int argc, char *argv[])
{
  /* Process Input Argumets and Set up corresponding UDP ports */
  process_arguments(argc, argv);

  // /* Run the program in background */
  // pid_t pid, sid;
  // pid = fork();
  // if (pid < 0) 
  // {
  //   printf("Error: subrouting fails\n");
  //   exit(1);
  // }
  // if (pid > 0) 
  // {
  //   exit(0);
  // }
  // sid = setsid();
  // if (sid < 0) 
  // {
  //   printf("Error: setsid\n");
  //   exit(1);
  // }
  // if (chdir("/") < 0) 
  // {
  //   perror("chdir");
  //   exit(1);
  // }
  // close(STDIN_FILENO);

  /* Real dvnode Starts Here */
  /* Initialize UDP */
  initialize_udp();

  /* Print Initial Table */
  gettimeofday(&tv, NULL);
  printf("[%ld.%06ld] Node <%d> Routing Table (Initial):\n%s", tv.tv_sec, tv.tv_usec, self_port, print_vector_list(head_dv));

  /* If it is the prgraom with argument [last], start sending */
  if(is_initial_sent == 2)
  {
    is_initial_sent = 1;
    send_dv_all();
  }

  for(;;)
  {
    if(flag_send == 1)
    {
      flag_send = 0;
      gettimeofday(&tv, NULL);
      printf("[%ld.%06ld] Node <%d> Routing Table:\n%s", tv.tv_sec, tv.tv_usec, self_port, print_vector_list(head_dv));

      send_dv_all();
    }
  }

  return 0;
}

/* Network Thread For Processing Incoming Messages */
void *network_thread_f(void *ignored)
{
  int n;
  char buffer[1024];
  unsigned int x = sizeof(client_addr);
  unsigned int *client_addr_len = &x;
  memset(buffer, 0, sizeof(buffer));
  
  while( (n = recvfrom(self_socket, (char *)buffer, 1024, 0, (struct sockaddr *)&client_addr, client_addr_len)) > 0 )
  {
    gettimeofday(&tv, NULL);
    printf("[%ld.%06ld] Message received at Node <%d> from Node <%d>\n", tv.tv_sec, tv.tv_usec, self_port, ntohs(client_addr.sin_port));

    int buffer_size;
    memcpy(&buffer_size, buffer, sizeof(int));

    if(debug_mode>1){printf("Rcvd Data: ");
    for (int i = 0; i < buffer_size*(sizeof(int) + sizeof(float)) + sizeof(int); i++) 
    {
      printf("%02X ", ((unsigned char*)buffer)[i]);
    }
    printf("\n");}

    struct distance_vector *dv_temp = buf_to_dv(buffer);
    int is_table_changed = 0;
    if(dv_temp != NULL)                                                                        // Distance Vector Algorithm
    {
      struct distance_vector *curr_dv;
      for (curr_dv = dv_temp; curr_dv != NULL; curr_dv = curr_dv->next)                        // Loop through all temps
      {
        struct distance_vector *temp_comp = find_vector(head_dv,curr_dv->node_num);
        struct edge_table *temp_weight = find_edge(head_et, ntohs(client_addr.sin_port));
        if(temp_weight == NULL)                                                                // This must not be null
        {
          printf("FatalError: Inconsistent table\n"); 
          exit(1);
        }
        else if(curr_dv->node_num == self_port)                                                // Ignore distance to self
        {
          ;
        }
        else
        {
          curr_dv->distance += temp_weight->distance;
          if(temp_comp == NULL)                                                             // Not Exist, effective to infinity
          {
            is_table_changed = 1;
            struct distance_vector *temp_insert = create_vector(curr_dv->node_num,curr_dv->distance);
            temp_insert->next_hop = ntohs(client_addr.sin_port);
            insert_vector(&head_dv,temp_insert);
          }
          else if(curr_dv->distance < temp_comp->distance)                                  // Bellman-Ford Equation
          {
            is_table_changed = 1;
            temp_comp->next_hop = ntohs(client_addr.sin_port);
            temp_comp->distance = curr_dv->distance;
          }
        }
      }
    }

    if(is_table_changed == 1 || is_initial_sent == 0)                                // flag_send can only be changed once per received packet 
    {
      is_table_changed = 0;
      is_initial_sent = 1;
      flag_send = 1;
    }
  }

  return NULL;
}

/* Processing Arguments */
int process_arguments(int argc, char *argv[])  
{
  if(argc < 4)                                                                       // Too few arguments
  {
    printf("Error: Not enough arguments.\n");
    printf("Input should be in form: ./dvnode <local-port> <neighbor1-port> <loss-rate-1> ... [last]\n");
    exit(1);
  }
  else if(strlen(argv[1]) > 6 || (self_port = str_to_portnum(argv[1])) == -1)       // Invalid self port number
  {
    printf("Error: Invalid self-port number, note that port number must be >1024.\n");
    exit(1);
  }
  else if(argc%2 == 1 && strcmp(argv[argc-1], "last") != 0)
  {
    printf("Error: Not enough arguments.\n");
    printf("Input should be in form: ./dvnode <local-port> <neighbor1-port> <loss-rate-1> ... [last]\n");
    exit(1);
  }
  
  for(int i = 1; 2*i < argc; i++)                                                      // Check rest of arguments
  {
    int temp_port = -1;
    float temp_distance = 0.0;

    if(strcmp(argv[2*i], "last") == 0)
    {
      if(2*i != argc - 1)
      {
        printf("Error: [last] statement should be the last.\n");
        exit(1);
      }
      else
      {
        is_initial_sent = 2;
        if(debug_mode>0){printf("Last\n");}
      }
    }
    else if((temp_port = str_to_portnum(argv[2*i])) == -1)                            // Validity of port number
    {
      printf("Invalid port number input: %s.\n",argv[2*i]);
      exit(1);
    }
    else if((temp_distance = str_to_prob(argv[2*i+1])) == -1.0)                       // Validity of distance
    {
      printf("Invalid distance input: %s.\n",argv[2*i+1]);
      exit(1);
    }
    else if(self_port == temp_port)                                                   // Check self-referencing
    {
      printf("Port can not equal to itself: %s.\n",argv[2*i]);
      exit(1);
    }
    else if(find_edge(head_et,temp_port) != NULL)                                     // Duplicate port number input
    {
      printf("Duplicate Adjacent Port input: %s.\n",argv[2*i]);
      exit(1);
    }
    else                                                                                // All good, insert
    {
      insert_vector(&head_dv, create_vector(temp_port ,temp_distance));
      insert_edge(&head_et, create_edge(temp_port, temp_distance));
    }
    
  }

  return 0;
}

// Note: atoi will still return valid inputs for, say 1235asd
// Implementation using strtol here
/* Convert String to Portnumber, invalid input or out-of range return -1 */
int str_to_portnum(char *str)
{
  char* endptr;
  int port_temp = strtol(str, &endptr, 10);

  if (*endptr == '\0') 
  {
    if (port_temp >= 1025 && port_temp <= 65535)            // Note that any portnum > 1024 is invalid as specified in assignment description
    {
      return port_temp;
    }
  }
  return -1;
}

/* Convert String to Probability, invalid input or out-of range return -1 */
double str_to_prob(char *str)
{
  char* endptr;
  double prob_temp = strtod(str, &endptr);

  if(debug_mode>0){printf("Temp: %f\n", prob_temp);}

  if (*endptr == '\0') 
  {
    if (prob_temp >= 0.0 && prob_temp <= 1.0)
    {
      return prob_temp;
    }
  }
  return -1.0;
}


















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
  client_addr.sin_port = htons(self_port);                // Trivial initialization of client addr

  char ip_address[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client_addr.sin_addr), ip_address, INET_ADDRSTRLEN);
  int port_number = ntohs(client_addr.sin_port);

  if(debug_mode>0){printf("Client IP address: %s\n", ip_address);}
  if(debug_mode>0){printf("Client port number: %d\n", port_number);}
  
  // Bind the socket with the server address
  if (bind(self_socket, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    printf("Error: Failed to Bind Port\n");
    exit(1);
  }

  // Start Network Thread
  if(pthread_create(&network_thread, NULL, network_thread_f, NULL)!=0)
  {
    printf("Error Binding Thread, exiting\n");
    exit(1);
  }
}

/* Send Message Using UDP Sockets */
void send_msg(int self_socket, long send_sequence, char header, char *buffer, struct sockaddr_in destaddr)
{
  char *msg_buf = malloc(1024);  // This is well beyond the intended use for 16 nodes, So I did not bother to send them in multiple UDPs

  // memcpy send sequence and header here

  if(buffer != NULL)
  {
    int buffer_size;
    memcpy(&buffer_size, buffer, sizeof(int));
    memcpy(msg_buf, buffer, buffer_size*(sizeof(int) + sizeof(float)) + sizeof(int)); // + sizeof(long) + sizeof(char)

    if(debug_mode>1){printf("To send: ");
    for (int i = 0; i < buffer_size*(sizeof(int) + sizeof(float)) + sizeof(int); i++) 
    {
      printf("%02X ", ((unsigned char*)msg_buf)[i]);
    }
    printf("\n");}

    if(sendto(self_socket, msg_buf, buffer_size*(sizeof(int) + sizeof(float)) + sizeof(int), 0, (const struct sockaddr *)&destaddr, sizeof(destaddr)) < 0)
    {
      printf("Error: Send fail, exiting");
      exit(0);
    }
    else
    {
      gettimeofday(&tv, NULL);
      printf("[%ld.%06ld] Message sent from Node <%d> to Node <%d>\n", tv.tv_sec, tv.tv_usec, ntohs(client_addr.sin_port), self_port);
    }
  }

  return;
}

/* Send distance vectors to all adjacent nodes */
void send_dv_all()
{
  // Conver dv to sendable string and get its size at dv_size
  char *dv_buf = dv_to_buf();
  struct edge_table *curr_et;
  
  for (curr_et = head_et; curr_et != NULL; curr_et = curr_et->next) 
  {
    // printf("Send to: %d\n",curr_et->node_num);     
    client_addr.sin_port = htons(curr_et->node_num);
    send_msg(self_socket, 0UL, 'a', dv_buf, client_addr);
  }
}

/* Convert Distance Vector to UDP buffer*/
char *dv_to_buf() // Format is as follows (int) <count Starting position> (int0) (float0) (int1) (float1) ... With first int storing the size of output
{
  char *output = malloc(1000);
  int count = sizeof(int); // Remembering where the memcpy should be at

  struct distance_vector *curr_dv;

  for(curr_dv = head_dv; curr_dv != NULL; curr_dv = curr_dv->next)
  {
    memcpy((unsigned char *)(output + count), (unsigned char *)&(curr_dv->node_num), sizeof(int));
    count += sizeof(int);
    memcpy((unsigned char *)(output + count), (unsigned char *)&(curr_dv->distance), sizeof(float));
    count += sizeof(float);
  }

  int num_of_count = (count - sizeof(int))/(sizeof(int)+sizeof(float));
  memcpy((unsigned char *)(output), (unsigned char *)&num_of_count, sizeof(int)); // Write total count as first integer

  if(debug_mode>0)
  {
    printf("Count: %d\n",count);
    printf("Cal Count: %lu\n", num_of_count*(sizeof(int) + sizeof(float)) + sizeof(int));

    for (int i = 0; i < count; i++) 
    {
      printf("%02X ", ((unsigned char*)output)[i]);
    }
  }
  return output;
}

/* Convert UDP to Distance Vector, Return the head of linked list if all data are valid, else return NULL */
struct distance_vector *buf_to_dv(char *buffer)
{
  struct distance_vector *head_temp = NULL;                      // Temporary head used to store incoming distance vector

  int count = sizeof(int);
  int buffer_size;
  memcpy(&buffer_size, buffer, sizeof(int));

  if(debug_mode>0){printf("BUFFER SIZE %d\n",buffer_size);}

  while (count < buffer_size*(sizeof(int)+sizeof(float)))
  {
    int node_num_temp;
    float distance_temp;
    memcpy(&node_num_temp, buffer + count, sizeof(int));
    count += sizeof(int);
    memcpy(&distance_temp, buffer + count, sizeof(float));
    count += sizeof(float);

    if(debug_mode>-1){printf("node_num: %d, distance: %f\n", node_num_temp, distance_temp);}

    if((node_num_temp >= 1025 && node_num_temp <= 65535))
    {
      insert_vector(&head_temp, create_vector(node_num_temp,distance_temp));
    }
    else
    {
      return NULL;
    }
  }
  return head_temp;
}
