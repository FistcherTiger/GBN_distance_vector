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
#include "cnnode.h"
#include "vector.h"

#define PACKET_PER_SECOND 1                                    // Decides how many packets (multiples of 20) to be send per second

/* Stored Data */
struct distance_vector *head_dv = NULL;                        // Header distance vector  (forward)
struct edge_table *head_et = NULL;                             // Header routing table    (forward)  have sender_receiver info
struct edge_table *head_p = NULL;                              // Store p_probability of each edge  (backward), have adjacency info and send_seq, recv_seq stored
 
void recv_GBN(char *buffer, long a, char b, struct edge_table *tmp); // Same as gbnnode
void hi_handler(int signum);
void timer2_handler(int signum);
void calculate_distance();

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

int *is_window_sent;                                           // Array for flagging whether a packt in sending window is sent, 1 sent, 0 not

int is_initial_sent = 0;                                 // Flagging whether the program have sent its distance vectors to anyone, 0 not yet, 1 sent, 2 last
int flag_send = 0;                                       // Flagging whether the distance vector should be sent out
int send_or_recv = -1;                                   // Indicate whether the edge is forward or backward

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

  /* Initialize variables */
  is_window_sent = (int*)malloc(window_size*sizeof(int)); // Send sliding window
  memset(is_window_sent, 0, window_size*sizeof(int));

  // Initialize the timers
  timer2.it_value.tv_sec = 1;
  timer2.it_interval.tv_sec = 1;
  signal(SIGVTALRM, timer2_handler);
  setitimer(ITIMER_VIRTUAL, &timer2, NULL); // Trigger event every second

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
    if(flag_send == 1)                       // Send dv updates, high priority
    {
      //printf("FLAG\n");
      pthread_mutex_lock(&mutex);
      flag_send = 0;
      pthread_mutex_unlock(&mutex);

      gettimeofday(&tv, NULL);
      printf("[%ld.%06ld] Node <%d> Routing Table:\n%s", tv.tv_sec, tv.tv_usec, self_port, print_vector_list(head_dv));

      send_dv_all();
    }
    else if (is_initial_sent == 1)
    {
      if(timer2_trigger == 1 && (timer2_counter>=1 && timer2_counter<=3))     // Send probing packets (20 per second) from interval 1 to 4
      {
        //printf("Probe\n");
        pthread_mutex_lock(&mutex);
        timer2_trigger = 0;
        pthread_mutex_unlock(&mutex);
        for(int i=0;i<PACKET_PER_SECOND;i++)
        {
          send_prob();
        }
      }
      else if (timer2_trigger == 1 && timer2_counter == 4)    // Send DV update every 5th second (note a timer2_counter cycle goes 0 -> 1 -> 2 -> 3 -> 4 -> 0)
      {
        //printf("dv\n");
        gettimeofday(&tv, NULL);
        printf("[%ld.%06ld] Node <%d> Routing Table:\n%s", tv.tv_sec, tv.tv_usec, self_port, print_vector_list(head_dv));
        pthread_mutex_lock(&mutex);
        timer2_trigger = 0;
        pthread_mutex_unlock(&mutex);
        calculate_distance();
        send_dv_all();
      }
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
    if(debug_mode>1)
    {
      printf("Rcvd Data: ");
      for (int i = 0; i < sizeof(long) + sizeof(char) + sizeof(int); i++) 
      {
        printf("%02X ", ((unsigned char*)buffer)[i]);
      }
      printf("\n");
    }

    long a;                                 // Received Sequence Number, negative is ACK
    char b;                                 // Received Char
    memcpy(&a, buffer, sizeof(long));
    memcpy(&b, buffer + sizeof(long), sizeof(char));

    if(debug_mode==1){printf("Long: %lu, Char %c\n",a,b);}

    if( b == 'd')                           // Distance Vector packet
    {
      gettimeofday(&tv, NULL);
      printf("[%ld.%06ld] Message received at Node <%d> from Node <%d>\n", tv.tv_sec, tv.tv_usec, self_port, ntohs(client_addr.sin_port));

      int buffer_size;
      memcpy(&buffer_size, buffer + sizeof(long) + sizeof(char), sizeof(int));

      if(debug_mode==1){printf("%d Rcvd Data: ",buffer_size);
      for (int i = 0; i < buffer_size*(sizeof(int) + sizeof(float)) + sizeof(int) + sizeof(long) + sizeof(char); i++) 
      {
        printf("%02X ", ((unsigned char*)buffer)[i]);
      }
      printf("\n");}

      char *buf_tp = buffer + sizeof(long) + sizeof(char);   // Skip the long and cahr
      struct distance_vector *dv_temp = buf_to_dv(buf_tp);
      int is_table_changed = 0;

      //printf("From <%d> DV:\n%s", ntohs(client_addr.sin_port), print_vector_list(dv_temp));

      if(dv_temp != NULL)                                                                        // Distance Vector Algorithm
      {
        struct distance_vector *curr_dv;
        for (curr_dv = dv_temp; curr_dv != NULL; curr_dv = curr_dv->next)                        // Loop through all temps
        {
          struct distance_vector *temp_comp = find_vector(head_dv,curr_dv->node_num);
          struct edge_table *temp_weight = find_edge(head_et, ntohs(client_addr.sin_port));
          if(temp_weight == NULL)                                                                // No forward edge to this place, thus no route possible
          {
            if(debug_mode==1){printf("Notfound %d\n\n",ntohs(client_addr.sin_port));}
          }
          else if(curr_dv->node_num == self_port)                                               // Ignore distance to self
          {
            ;
          }
          else
          {
            if(debug_mode==1){printf("dvDis: %f, tpDis:%f\n", curr_dv->distance, temp_weight->distance);}
            curr_dv->distance += temp_weight->distance;
            if(temp_comp == NULL)                                                             // Not Exist, effective to infinity
            {
              //printf("Insert\n");
              is_table_changed = 1;
              struct distance_vector *temp_insert = create_vector(curr_dv->node_num,curr_dv->distance);
              temp_insert->next_hop = ntohs(client_addr.sin_port);
              insert_vector(&head_dv,temp_insert);
            }
            else if(curr_dv->distance < temp_comp->distance)                                  // Bellman-Ford Equation
            {
              //printf("Bellman-Ford\n");
              is_table_changed = 1;
              temp_comp->next_hop = ntohs(client_addr.sin_port);
              temp_comp->distance = curr_dv->distance;
            }
          }
        }
      }
      
      if(is_table_changed == 1 || is_initial_sent == 0)                                // flag_send can only be changed once per received packet 
      {
        //printf("flag_send = 1\n");
        is_table_changed = 0;
        is_initial_sent = 1;
        pthread_mutex_lock(&mutex);
        flag_send = 1;
        pthread_mutex_unlock(&mutex);
      }
    }
    else if( b == 'p')                                                                        // Probe Packets
    {
      struct edge_table *tmp = find_edge(head_p,ntohs(client_addr.sin_port));
      float p_probability = tmp->distance;
      float r = (float)rand() / RAND_MAX;

      tmp->total_ct++;
      if(tmp->total_ct % 200 == 0)
      {
        //printf("Got prob\n");
        //timer_handler2();
      }

      if (r > p_probability) 
      {
        recv_GBN(buffer,a,b,tmp);
      }
      else
      {
        if(a<0)
        {
          if(debug_mode == 1){
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] ACK%ld discarded\n", tv.tv_sec, tv.tv_usec, -a);}
        }
        else if(a>0)
        {
          if(debug_mode == 1){
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] Packet%ld discarded\n", tv.tv_sec, tv.tv_usec, a);}
          send_msg(self_socket, 0UL, 'n', NULL, client_addr); // Send NAK
        }
      }
    }
    else if( b == 'n')  // Notify sender reciver dropped packet
    {
      struct edge_table *tmp = find_edge(head_p,ntohs(client_addr.sin_port));

      tmp->faliure_ct++;
      tmp->total_ct++;
      if(tmp->total_ct % 200 == 0)
      {
        //printf("Got prob\n");
        //timer_handler2();
      }
    }
  }

  return NULL;
}

/* Processing Arguments */
int process_arguments(int argc, char *argv[])  
{
  if(argc < 5)                                                                       // Too few arguments
  {
    printf("Error: Not enough arguments.\n");
    printf("Input should be in form: ./dvnode <local-port> receive <neighbor1-port> <loss-rate-1> ... send ... [last]\n");
    exit(1);
  }
  else if(strlen(argv[1]) > 6 || (self_port = str_to_portnum(argv[1])) == -1)       // Invalid self port number
  {
    printf("Error: Invalid self-port number, note that port number must be >1024.\n");
    exit(1);
  }
  else if(strcmp(argv[2], "receive") != 0)
  {
    printf("Error: Second argument must be receive.\n");
    printf("Input should be in form: ./dvnode <local-port> receive <neighbor1-port> <loss-rate-1> ... send ... [last]\n");
    exit(1);
  }
  
  for(int i = 3; i < argc;)                                                      // Check rest of arguments,
  {

    if(strcmp(argv[i], "receive") == 0)                                              // Should not have another receive
    {
      printf("Error: Duplicate receive.\n");
      printf("Input should be in form: ./dvnode <local-port> receive <neighbor1-port> <loss-rate-1> ... send ... [last]\n");
      exit(1);
    }
    if(strcmp(argv[i], "send") == 0)                                                 // Switch to send
    {
      if(send_or_recv == -1)
      {
        send_or_recv = 1;
        i++;
        continue;
      }
      else
      {
        printf("Error: Duplicate send.\n");
        printf("Input should be in form: ./dvnode <local-port> receive <neighbor1-port> <loss-rate-1> ... send ... [last]\n");
        exit(1);
      }
    }
    if(i == argc-1 && send_or_recv == -1)                                      // Already At end yet no "send" keyword found 
    {
      printf("Error: Missing Send.\n");
      printf("Input should be in form: ./dvnode <local-port> receive <neighbor1-port> <loss-rate-1> ... send ... [last]\n");
      exit(1);
    }

    if(strcmp(argv[i], "last") == 0)                                           // Check last statement
    {
      if(i != argc-1)
      {
        printf("Error: [last] statement should be the last.\n");
        exit(1);
      }
      else
      {
        is_initial_sent = 2;
        if(debug_mode>0){printf("Last\n");}
        break;
      }
    }

    if(send_or_recv == -1)                                                         // Reading receive
    {
      int temp_port = -1;
      float temp_distance = 0.0;
      if(i+1>=argc)
      {
        printf("Error: Not enough arguments.\n");
        printf("Input should be in form: ./dvnode <local-port> <neighbor1-port> <loss-rate-1> ... [last]\n");
        exit(1);
      }
      else if((temp_port = str_to_portnum(argv[i])) == -1)                            // Validity of port number
      {
        printf("Invalid port number input: %s.\n",argv[i]);
        exit(1);
      }
      else if(self_port == temp_port)                                                   // Check self-referencing
      {
        printf("Port can not equal to itself: %s.\n",argv[i]);
        exit(1);
      }
      else if(find_edge(head_et,temp_port) != NULL)                                     // Duplicate port number input
      {
        printf("Duplicate Adjacent Port input: %s.\n",argv[i]);
        exit(1);
      }
      else if((temp_distance = str_to_prob(argv[i+1])) == -1.0)                       // Validity of distance
      {
        printf("Invalid distance input: %s.\n",argv[i+1]);
        exit(1);
      }
      else                                                                                // All good, insert
      {
        insert_edge(&head_p, create_edge(temp_port, temp_distance));                   // Receving edge have a fixed chance of packet fail
        i+=2;
      }
    }
    else if(send_or_recv == 1)
    {
      int temp_port = -1;
      if((temp_port = str_to_portnum(argv[i])) == -1)                            // Validity of port number
      {
        printf("Invalid port number input: %s.\n",argv[i]);
        exit(1);
      }
      else if(self_port == temp_port)                                                   // Check self-referencing
      {
        printf("Port can not equal to itself: %s.\n",argv[i]);
        exit(1);
      }
      else if(find_edge(head_et,temp_port) != NULL)                                     // Duplicate port number input
      {
        printf("Duplicate Adjacent Port input: %s.\n",argv[i]);
        exit(1);
      }
      else                                                                                // All good, insert
      {
        insert_vector(&head_dv, create_vector(temp_port , 0.0));
        insert_edge(&head_et, create_edge(temp_port, 0.0));
        insert_edge(&head_p,create_edge(temp_port, 0.0));                                // Sending edge have 0% chance of fail
        i+=1;
      }
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
float str_to_prob(char *str)
{
  char* endptr;
  float prob_temp = strtod(str, &endptr);

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

  memcpy(msg_buf, &send_sequence, sizeof(long));
  memcpy(msg_buf + sizeof(long), &header, sizeof(char));

  if(buffer != NULL) // DV updates
  {
    int buffer_size;
    memcpy(&buffer_size, buffer, sizeof(int));
    memcpy(msg_buf + sizeof(long) + sizeof(char), buffer, buffer_size*(sizeof(int) + sizeof(float)) + sizeof(int)); 

    if(debug_mode == 1 && header == 'd'){printf("To send: ");
    for (int i = 0; i < buffer_size*(sizeof(int) + sizeof(float)) + sizeof(int) + sizeof(long) + sizeof(char); i++) 
    {
      printf("%02X ", ((unsigned char*)msg_buf)[i]);
    }
    printf("\n");}

    if(sendto(self_socket, msg_buf, buffer_size*(sizeof(int) + sizeof(float)) + sizeof(int) + sizeof(char) + sizeof(long), 0, (const struct sockaddr *)&destaddr, sizeof(destaddr)) < 0)
    {
      printf("Error: Send fail, exiting");
      exit(0);
    }
    else
    {
      gettimeofday(&tv, NULL);
      printf("[%ld.%06ld] Message sent from Node <%d> to Node <%d>\n", tv.tv_sec, tv.tv_usec, self_port, ntohs(destaddr.sin_port));
    }
  }
  else if(sendto(self_socket, msg_buf, sizeof(long) + sizeof(char), 0, (const struct sockaddr *)&destaddr, sizeof(destaddr)) < 0) // Probe packets
  {
    printf("Error: Send fail, exiting");
    exit(0);
  }

  return;
}

/* Send distance vectors to all adjacent nodes */
void send_dv_all()
{
  // Conver dv to sendable string and get its size at dv_size
  char *dv_buf = dv_to_buf();
  struct edge_table *curr_et;
  
  for (curr_et = head_p; curr_et != NULL; curr_et = curr_et->next) 
  {
    // printf("Send to: %d\n",curr_et->node_num);     
    client_addr.sin_port = htons(curr_et->node_num);
    send_msg(self_socket, 0UL, 'd', dv_buf, client_addr);
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

    if(debug_mode>0){printf("node_num: %d, distance: %f\n", node_num_temp, distance_temp);}

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

/* Send probing packets to edges */
void send_prob()
{
  if(head_et == NULL)       // No foward edge to send to
  {
    return;
  }

  /* Simulate send pppppppppppppppppppp in gbnnode */
  char arg1[20] = {0};                                                                
  memset(arg1, 'p', sizeof(arg1)-1);

  int send_buf_size = 2*window_size;                                                 // Send buf size is 2*window size  

  if(debug_mode==1){printf("Send prob on %d",self_port);}
  
  struct edge_table *et_temp;
  for(et_temp = head_et; et_temp != NULL; et_temp = et_temp->next)               // Loop through all forward edges
  {
    // prev_sequence is fwd_temp->prev_seq;
    // send_sequence is fwd_temp->send_seq;
    // recv_sequence is fwd_temo->recv_seq

    if(debug_mode==1){printf("Send prob to %d",et_temp->node_num);}

    struct edge_table *fwd_temp = find_edge(head_p, et_temp->node_num);         // Store actual sequence in head_p

    /* Original code from GBN */
    fwd_temp->prev_seq = fwd_temp->send_seq;                                                  // Anchor the position before sending
    int len_input = strlen(arg1);

    //is_send = true;
    is_timeout = false;
    is_ack = 0;                                                                               // 0 means packet not sent, 1 otherwise
    memset(is_window_sent, 0, window_size*sizeof(int));

    char send_buf[send_buf_size];                                                      
    strncpy(send_buf, arg1, send_buf_size);

    if(debug_mode>1)
    {
      printf("send_buf: ");
      for(int v = 0; v <= strlen(send_buf); v++)
      {
        printf("%c",send_buf[v]);
      }
      printf("\n");
    }

    while(fwd_temp->send_seq - fwd_temp->prev_seq < len_input)
    {
      pthread_mutex_lock(&mutex);
      if(debug_mode==1){printf("Mutex Lock In\n");}

      if(is_timeout)
      {
        if(debug_mode==1)
        {
          printf("Timeout Alarm Set\n");
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] packet%ld %c timeout\n", tv.tv_sec, tv.tv_usec, fwd_temp->send_seq, send_buf[(fwd_temp->send_seq - fwd_temp->prev_seq)%send_buf_size]);
        }
        is_timeout = false;
        memset(is_window_sent, 0, window_size*sizeof(int)); // All packets in updated/unupdated window needs to be resend
      }
      if(is_ack != 0)                                       // Window has moved 1) fill sent_buf with new inputs, 2)reset timer
      {
        if(debug_mode==1){printf("ACK Alarm Set\n");}       

        for(int i = is_ack; i>0; i--)                       // 1) fill sent_buf with new inputs
        {
          if(len_input > 2*window_size + (fwd_temp->send_seq - fwd_temp->prev_seq - i))
          {
            if(debug_mode==1)
            {
              printf("send_buf: ");
              for(int v = 0; v <= strlen(send_buf); v++)
              {
                printf("%c",send_buf[v]);
              }
              printf("\n");
            }
            if(debug_mode>1){printf("Before: %c\n", send_buf[(fwd_temp->send_seq - fwd_temp->prev_seq - i)%(2*window_size)]);}

            send_buf[(fwd_temp->send_seq - fwd_temp->prev_seq - i)%(2*window_size)] = arg1[fwd_temp->send_seq - fwd_temp->prev_seq - i + 2*window_size];
        
            if(debug_mode>1){printf("After: %c\n", arg1[fwd_temp->send_seq - fwd_temp->prev_seq - i + 2*window_size]);}
          }
        }

        is_ack = 0;                                         // 2)reset timer
        signal(SIGALRM, timer_handler);
        timer.it_value.tv_usec = 500000;
        setitimer(ITIMER_REAL, &timer, NULL);

      }

      for(int send_base = 0; send_base < window_size; send_base++) // Actual sending mechanism
      {
        if(is_window_sent[send_base] == 0 && fwd_temp->send_seq - fwd_temp->prev_seq + send_base < len_input) // Not sent & in range, send packet
        {
          if(debug_mode==1)
          {
            gettimeofday(&tv, NULL);
            printf("[%ld.%06ld] packet%ld %c sent\n", tv.tv_sec, tv.tv_usec, fwd_temp->send_seq + send_base, send_buf[(fwd_temp->send_seq - fwd_temp->prev_seq + send_base)%send_buf_size]);
          }

          is_window_sent[send_base] = 1;

          client_addr.sin_port = htons(fwd_temp->node_num);
          send_msg(self_socket, fwd_temp->send_seq + send_base, send_buf[(fwd_temp->send_seq - fwd_temp->prev_seq + send_base)%send_buf_size], NULL, client_addr);
          
          if(send_base == 0)                                                           // If it is the first packet sent, start timer
          {
            if(debug_mode==1){printf("First Alarm Set\n");}
            signal(SIGALRM, timer_handler);
            timer.it_value.tv_usec = 500000;
            setitimer(ITIMER_REAL, &timer, NULL); 
          }
        }
      }

      while (!is_timeout && is_ack == 0)                                                   // Wait for timeout or ACK
      {
        if(debug_mode>1){printf("Start Hearing: \n");}
        pthread_cond_wait(&cond, &mutex);
      }
      if(debug_mode==1){printf("Mutex Lock out, ACK: %d, Timeout: %d\n",is_ack,is_timeout);}
      pthread_mutex_unlock(&mutex);

    }

  }
}


void move_window(int x) // X is number of times to move the window
{
  for(int i = 0; i < x; i++)
  {
    for(int j = 0; j < window_size; j++)                // Move window
    {
      is_window_sent[j] = is_window_sent[j+1];
    }
  }
}

void recv_GBN(char *buffer, long a, char b, struct edge_table *tmp)
{
  // prev_sequence is tmp->prev_seq
  // send_sequence is tmp->send_seq
  // recv_sequence is tmp->recv_seq

  if(debug_mode>1){printf("RECV_GBN: %ld, %c\n", a, b);}

  if(a < 0 && -a >= tmp->send_seq)       // If sequence number is negative, it is an ACK
  { 
    if(debug_mode==1){gettimeofday(&tv, NULL);
    printf("[%ld.%06ld] ACK%ld received, window moves to %ld\n", tv.tv_sec, tv.tv_usec, -a, (-a+1 - tmp->prev_seq)%(2*window_size)+1);}

    pthread_mutex_lock(&mutex);

    is_ack += -a + 1 - tmp->send_seq;
    timer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    move_window(is_ack);                // Update sent window
    tmp->send_seq = -a + 1;
    
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
  }
  else if(a < 0 && -a < tmp->send_seq)
  {
    if(debug_mode==1){gettimeofday(&tv, NULL);
    printf("[%ld.%06ld] ACK%ld received, window moves to %ld\n", tv.tv_sec, tv.tv_usec, -a, (tmp->send_seq - tmp->prev_seq)%(2*window_size)+1);}
  }
  else if(a > 0)                         // Otherwise it is data packet, send ACK
  {
    if(a == tmp->recv_seq)
    {
      if(debug_mode==1)
      {
        gettimeofday(&tv, NULL);
        printf("[%ld.%06ld] Packet%ld %c received\n", tv.tv_sec, tv.tv_usec, a, b);
      }

      send_msg(self_socket, -tmp->recv_seq, b, NULL, client_addr);

      if(debug_mode==1)
      {
        gettimeofday(&tv, NULL);
        printf("[%ld.%06ld] ACK%ld sent, expecting packet%ld\n", tv.tv_sec, tv.tv_usec, tmp->recv_seq, tmp->recv_seq+1);
      }

      tmp->recv_seq++;
    }
    else
    {
      if(tmp->recv_seq > 1)
      {
        if(debug_mode==1)
        {
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] Packet%ld %c received\n", tv.tv_sec, tv.tv_usec, a, b);
        }

        send_msg(self_socket, -tmp->recv_seq+1, b, NULL ,client_addr);

        if(debug_mode==1)
        {
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] ACK%ld sent, expecting packet%ld\n", tv.tv_sec, tv.tv_usec, tmp->recv_seq-1, tmp->recv_seq);
        }
      }
      else
      {
        if(debug_mode==1)
        {
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] Packet%ld %c received\n", tv.tv_sec, tv.tv_usec, a, b);
          printf("Special Case, no ACKs will be sent\n");
        }
      }
    }
  }
}

/* Timeout Event Handler */
void timer_handler()
{
  if(debug_mode == 1){printf("Handler Called\n");}
  pthread_mutex_lock(&mutex);
  is_timeout = true;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&mutex);
}

/* Update Distances for all sender_receiver pair, remove all extra nodes and update dv as initialized */
void calculate_distance()
{
  int send_flag = 0;
  struct edge_table *curr_et;                                     // Update Distances for all sender_receiver pair

  struct distance_vector *new_head_dv = NULL;
  struct distance_vector *temp_head_dv = NULL;

  for (curr_et = head_et; curr_et != NULL; curr_et = curr_et->next) 
  {
    struct edge_table *temp_p = find_edge(head_p, curr_et->node_num);
    struct distance_vector *temp_v = find_vector(head_dv, curr_et->node_num);

    float calculated_distance = round_to_two_decimals((float) temp_p->faliure_ct / temp_p->total_ct);
    //printf("Edge: %d Fai: %lu, Tot: %lu, Cal: %.2f\n", temp_p->node_num, temp_p->faliure_ct, temp_p->total_ct, calculated_distance);

    if(curr_et->distance != calculated_distance) // If there is a change in rounded distance, send dv updates
    {
      pthread_mutex_lock(&mutex);
      send_flag = 1;
      pthread_mutex_unlock(&mutex);
    }
    
    curr_et->distance = calculated_distance;
    temp_v->distance = calculated_distance;

    insert_vector(&new_head_dv, create_vector(curr_et->node_num,curr_et->distance));
  }

  temp_head_dv = head_dv; // Reset the list to the original value
  head_dv = new_head_dv;
  destroy_list(&temp_head_dv);

  if(send_flag == 1)
  {
    send_dv_all();
  }

  //printf("[%ld.%06ld] Node <%d> Routing Table (handler):\n%s", tv.tv_sec, tv.tv_usec, self_port, print_vector_list(head_dv));
  //printf("Forward Table:\n");
  //print_edge(head_et);
  //printf("Backward Table\n");
  //print_edge(head_p);
}


void timer2_handler(int signum) 
{
  //printf("Timer2: count %d trigger %d\n", timer2_counter, timer2_trigger);
  pthread_mutex_lock(&mutex);
  timer2_counter = (timer2_counter + 1) % 5;
  if(timer2_trigger == 0)
  {
    timer2_trigger = 1;
  }
  pthread_mutex_unlock(&mutex);

  /* Calculate Rate */
  struct edge_table *tp_et;
  for(tp_et = head_et; tp_et != NULL; tp_et = tp_et->next)
  {
    struct edge_table *tp_p = find_edge(head_p, tp_et->node_num);
    if(is_initial_sent>0)
    {
      gettimeofday(&tv, NULL);
      printf("[%ld.%06ld] Link to %d: %lu packets sent, %lu packets lost, loss rate %.2f  \n", tv.tv_sec, tv.tv_usec, tp_et->node_num, tp_p->total_ct-1, tp_p->faliure_ct, round_to_two_decimals((float) tp_p->faliure_ct / tp_p->total_ct));
    }
  }
}

float round_to_two_decimals(float num) 
{
  return (float) ((int) (num * 100 + 0.5)) / 100;
}