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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

/* Custom Headers */
#include "gbnnode.h"
#include "udp.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

int *is_window_sent;                                           // Array for flagging whether a packt in sending window is sent, 1 sent, 0 not

int count_total = 0;                                          // Total packet heard
int count_success = 0;                                        // Total packet successfully heard

/* Main Thread For Processing Commands */
int main(int argc, char *argv[])
{
  /* Process Input Argumets and Set Up corresponding UDP ports */
  process_arguments(argc, argv);

  /* Initialize Variables */
  char input[200];
  memset(input, 0, sizeof(input));
  is_window_sent = (int*)malloc(window_size*sizeof(int));
  memset(is_window_sent, 0, window_size*sizeof(int));

  /* Send system involves mutex in sending, whcih can be broke by either timeout or is_ack */
  is_timeout = false;                                         // Flag for timeout in send system
  is_ack = 0;                                                 // Flag for ack in send system

  /* Currently Summary page is trigger by header 0 (reserved so far) I assumed it can be delivered 100% times */
  /* It can be done without such assumpsions, just integrate it into the recv_GBN, just more work */
  /* An alternative approach is (not finished in my code): use timeout event to trigger Summary Page at receiver if is_send is false */
  //is_send = false;                                          // Flag to signal whether the timer is being used by send-system

  /* Booting Check */
  if(debug_mode == 1)
  {
    printf("Self-Port: %d\n", self_port);
    printf("Peer-Port: %d\n", peer_port);
    printf("Window-Size: %d\n", window_size);
    printf("Interval: %d\n", d_interval);
    printf("Probability: %f\n", p_probability);
  }

  for(;;)
  {
    printf("node> ");
    fflush(stdout);
    fgets(input, 200, stdin);

    /* Process commands */
    char *command_buffer = strtok(input, "\n");           // Remove the line changer which will be present at the end
    char *command = strtok(command_buffer, " ");          // Partition
    if (command != NULL) 
    {
      process_command(command);
    }
  }

  return 0;

}

/* Network Thread For Processing Incoming Messages */
void *network_thread_f(void *ignored)
{
  int n;
  char buffer[sizeof(long) + sizeof(char)];

  unsigned int x = sizeof(client_addr);
  unsigned int *client_addr_len = &x;
  memset(buffer, 0, sizeof(buffer));
  
  while( (n = recvfrom(self_socket, (char *)buffer, sizeof(long) + sizeof(char), 0, (struct sockaddr *)&client_addr, client_addr_len)) > 0 )
  {
    long a;                                 // Received Sequence Number, negative is ACK
    char b;                                 // Received Char
    memcpy(&a, buffer, sizeof(long));
    memcpy(&b, buffer + sizeof(long), sizeof(char));

    if(a != 0UL){count_total++;}            // Add to count_total, if it is not terminal packet

    if(a == 0UL)
    {
      if(count_total!=0)
      {
        float loss_rate = (float) (count_total-count_success)/count_total;
        printf("[Summary] %d/%d packets discarded, loss rate = %f\n",count_total-count_success,count_total,loss_rate);
        printf("node> ");
        fflush(stdout);
      }
      else
      {
        printf("[Summary] 0/0 packets discarded, loss rate = 0\n");
      }
      count_total = 0;
      count_success = 0;
    }
    else if(p_probability != -1.0)
    {
      float r = (float)rand() / RAND_MAX;
      if (r > p_probability) 
      {
        count_success++;
        recv_GBN(buffer,a,b);
      }
      else
      {
        if(a<0)
        {
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] ACK%ld discarded\n", tv.tv_sec, tv.tv_usec, -a);
        }
        else if(a>0)
        {
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] Packet%ld discarded\n", tv.tv_sec, tv.tv_usec, a);
        }
      }
    }
    else if(d_interval != -1)
    {
      if(count_total % d_interval != 0)
      {
        count_success++;
        recv_GBN(buffer,a,b);
      }
      else
      {
        if(a<0)
        {
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] ACK%ld discarded\n", tv.tv_sec, tv.tv_usec, -a);
        }
        else if(a>0)
        {
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] Packet%ld discarded\n", tv.tv_sec, tv.tv_usec, a);
        }
      }
    }
  }

  return NULL;

}

/* Processing Arguments */
int process_arguments(int argc, char *argv[])  
{
  if(argc < 6)                                                              // Too few arguments
  {
    printf("Error: Not enough arguments.\n");
    printf("Input should be in form:./gbnnode <self-port> <peer-port> <window-size> [ -d <value-of-n> | -p <value-of-p>]\n");
    exit(1);
  }
  else if(argc > 6)                                                        // Too many arguments, after this is input validity check
  {
    printf("Error: Too many arguments.\n");
    printf("Input should be in form:./gbnnode <self-port> <peer-port> <window-size> [ -d <value-of-n> | -p <value-of-p>]\n");
    exit(1);
  }
  else if(strlen(argv[1]) > 6 || (self_port = str_to_portnum(argv[1])) == -1) // Invalid self port number
  {
    printf("Error: Invalid self-port number.\n");
    exit(1);
  }
  else if(strlen(argv[2]) > 6 || (peer_port = str_to_portnum(argv[2])) == -1) // Invalid peer port number
  {
    printf("Error: Invalid peer-port number.\n");
    exit(1);
  }
  else if(self_port == peer_port)                                          // Check if ports are equal
  {
    printf("Error: Self-port and peer-port can not have same port number\n");
    exit(1);
  }
  else if((window_size = str_to_window(argv[3])) == -1)                    // Invalid window size
  {
    printf("Error: Invalid window size, window size allows integer from 1 to %d.\n", MAX_WINDOW_SIZE);
    exit(1);
  }
  else if(strcmp(argv[4], "-d") == 0)                                            /* Deterministic Mode */
  {
    if(strlen(argv[5]) > 6 || (d_interval = str_to_interval(argv[5])) == -1)     // Check if interval is valid
    {
      printf("Error: Invalid interval size, interval size allows integer from 1 to %d.\n", MAX_INTERVAL);
      exit(1);
    }
    else
    {
      printf("Deterministic Mode with interval %d\n",d_interval);
      initialize_udp();
    }
  }
  else if(strcmp(argv[4], "-p") == 0)                                            /* Probabilistic Mode */
  {
    if(strlen(argv[5]) > 8 || (p_probability = str_to_prob(argv[5])) == -1.0)     // Check if probability is valid
    {
      printf("Error: Invalid probability, which should ranges from 0 to 1.\n");
      printf("Note that probability should not exceed 6 decimals\n");
      exit(1);
    }
    else
    {
      printf("Probabilistic Mode with chance %f\n", p_probability);
      initialize_udp();
    }
  }
  else                                                                         // Invalid Flag
  {
    printf("Error: Invalid mode of operation. Allowed mode are -d and -p only\n");
    exit(1);
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
    if (port_temp >= 1 && port_temp <= 65535)
    {
      return port_temp;
    }
  }
  return -1;
}

/* Convert String to Window Size, invalid input or out-of range return -1 */
int str_to_window(char *str)
{
  char* endptr;
  int window_temp = strtol(str, &endptr, 10);

  if (*endptr == '\0') 
  {
    if (window_temp >= 1 && window_temp <= MAX_WINDOW_SIZE)
    {
      return window_temp;
    }
  }
  return -1;
}

/* Convert String to Interval Size, invalid input or out-of range return -1 */
int str_to_interval(char *str)
{
  char* endptr;
  int inter_temp = strtol(str, &endptr, 10);

  if (*endptr == '\0') 
  {
    if (inter_temp >= 1 && inter_temp <= MAX_INTERVAL)
    {
      return inter_temp;
    }
  }
  return -1;
}

/* Convert String to Probability, invalid input or out-of range return -1 */
double str_to_prob(char *str)
{
  char* endptr;
  double prob_temp = strtod(str, &endptr);

  if(debug_mode==1){printf("Temp: %f\n", prob_temp);}

  if (*endptr == '\0') 
  {
    if (prob_temp >= 0.0 && prob_temp <= 1.0)
    {
      return prob_temp;
    }
  }
  return -1.0;
}

/* Processing Commands */
void process_command(char *command) 
{
  char arg1[1000] = "";           // get the first argument (if exists) Note that everything after the command is a single arg
  char *arg1_temp = strtok(NULL, " ");
  while(arg1_temp != NULL)
  {
    char *temp = " ";
    if(strcmp(arg1, "") != 0)
    {
      strcat(arg1,temp);
    }
    strcat(arg1,arg1_temp);
    arg1_temp = strtok(NULL, " ");
  }

  /* Switch Betwwen Commands */
  if (strcmp(command, "debug_mode") == 0)                                              // Debug_mode
  {
    if(strcmp(arg1, "1") == 0)
    {
      printf("Debug Mode On\n");
      debug_mode=1;
    }
    else if(strcmp(arg1, "0") == 0)
    {
      printf("Debug Mode Off\n");
      debug_mode=0;
    }
    else if(debug_mode == 1)
    {
      printf("Debug Mode Off\n");
      debug_mode=0;
    }
    else if(debug_mode == 0)
    {
      printf("Debug Mode On\n");
      debug_mode=1;
    }
  } 
  else if (strcmp(command, "send") == 0)                                               // Implement GBN Here
  {
    int send_buf_size = 2*window_size;                                                 // Send buf size is 2*window size
    prev_sequence = send_sequence;                                                     // Anchor the position before sending
    int len_input = strlen(arg1);

    //is_send = true;
    is_timeout = false;
    is_ack = 0;                                       // 0 means packet not sent, 1 otherwise
    memset(is_window_sent, 0, window_size*sizeof(int));

    char send_buf[send_buf_size];                                                      
    strncpy(send_buf, arg1, send_buf_size);

    if(debug_mode==1)
    {
      printf("send_buf: ");
      for(int v = 0; v <= strlen(send_buf); v++)
      {
        printf("%c",send_buf[v]);
      }
      printf("\n");
    }

    while(send_sequence - prev_sequence < len_input)
    {
      pthread_mutex_lock(&mutex);
      if(debug_mode==1){printf("Mutex Lock In\n");}

      if(is_timeout)
      {
        if(debug_mode==1){printf("Timeout Alarm Set\n");}
        gettimeofday(&tv, NULL);
        printf("[%ld.%06ld] packet%ld %c timeout\n", tv.tv_sec, tv.tv_usec, send_sequence, send_buf[(send_sequence - prev_sequence)%send_buf_size]);
        is_timeout = false;
        memset(is_window_sent, 0, window_size*sizeof(int)); // All packets in updated/unupdated window needs to be resend
      }
      if(is_ack != 0)                                       // Window has moved 1) fill sent_buf with new inputs, 2)reset timer
      {
        if(debug_mode==1){printf("ACK Alarm Set\n");}       

        for(int i = is_ack; i>0; i--)                       // 1) fill sent_buf with new inputs
        {
          if(len_input > 2*window_size + (send_sequence - prev_sequence - i))
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
            if(debug_mode==1){printf("Before: %c\n", send_buf[(send_sequence - prev_sequence - i)%(2*window_size)]);}

            send_buf[(send_sequence - prev_sequence - i)%(2*window_size)] = arg1[send_sequence - prev_sequence - i + 2*window_size];
        
            if(debug_mode==1){printf("After: %c\n", arg1[send_sequence - prev_sequence - i + 2*window_size]);}
          }
        }

        is_ack = 0;                                         // 2)reset timer
        signal(SIGALRM, timer_handler);
        timer.it_value.tv_usec = 500000;
        setitimer(ITIMER_REAL, &timer, NULL);

      }

      for(int send_base = 0; send_base < window_size; send_base++)
      {
        if(is_window_sent[send_base] == 0 && send_sequence - prev_sequence + send_base < len_input) // Not sent & in range, send packet
        {
          
          gettimeofday(&tv, NULL);
          printf("[%ld.%06ld] packet%ld %c sent\n", tv.tv_sec, tv.tv_usec, send_sequence + send_base, send_buf[(send_sequence - prev_sequence + send_base)%send_buf_size]);

          is_window_sent[send_base] = 1;
          send_msg(self_socket, send_buf[(send_sequence - prev_sequence + send_base)%send_buf_size], send_sequence + send_base, client_addr);
          
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
        if(debug_mode==1){printf("Start Hearing: \n");}
        pthread_cond_wait(&cond, &mutex);
      }
      if(debug_mode==1){printf("Mutex Lock out, ACK: %d, Timeout: %d\n",is_ack,is_timeout);}
      pthread_mutex_unlock(&mutex);

    }

    //is_send = false;
    send_msg(self_socket, '\0', 0UL, client_addr);                                         // Trigger summary page
    if(count_total!=0)                                                                     // Display own summary page
    {
      float loss_rate = (float) (count_total-count_success)/count_total;
      printf("[Summary] %d/%d packets discarded, loss rate = %f\n",count_total-count_success,count_total,loss_rate);
    }
    else
    {
      printf("[Summary] 0/0 packets discarded, loss rate = 0\n");
    }
    count_total = 0;
    count_success = 0;
  } 
  else 
  {
    printf("[Invalid command]\n");
    fflush(stdout);
  }
}

void recv_GBN(char *buffer, long a, char b)
{

  if(debug_mode==1){printf("RECV_GBN: %ld, %c\n", a, b);}

  if(a < 0 && -a >= send_sequence)       // If sequence number is negative, it is an ACK
  { 
    gettimeofday(&tv, NULL);
    printf("[%ld.%06ld] ACK%ld received, window moves to %ld\n", tv.tv_sec, tv.tv_usec, -a, (-a+1-prev_sequence)%(2*window_size)+1);

    pthread_mutex_lock(&mutex);

    is_ack += -a + 1 - send_sequence;
    timer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    move_window(is_ack);                // Update sent window
    send_sequence = -a + 1;
    
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
  }
  else if(a < 0 && -a < send_sequence)
  {
    gettimeofday(&tv, NULL);
    printf("[%ld.%06ld] ACK%ld received, window moves to %ld\n", tv.tv_sec, tv.tv_usec, -a, (send_sequence-prev_sequence)%(2*window_size)+1);
  }
  else if(a > 0)                         // Otherwise it is data packet, send ACK
  {
    if(a == recv_sequence)
    {
      gettimeofday(&tv, NULL);
      printf("[%ld.%06ld] Packet%ld %c received\n", tv.tv_sec, tv.tv_usec, a, b);

      send_msg(self_socket, b, -recv_sequence, client_addr);

      gettimeofday(&tv, NULL);
      printf("[%ld.%06ld] ACK%ld sent, expecting packet%ld\n", tv.tv_sec, tv.tv_usec, recv_sequence, recv_sequence+1);

      recv_sequence++;
    }
    else
    {
      if(recv_sequence > 1)
      {
        gettimeofday(&tv, NULL);
        printf("[%ld.%06ld] Packet%ld %c received\n", tv.tv_sec, tv.tv_usec, a, b);

        send_msg(self_socket, b, -recv_sequence+1, client_addr);

        gettimeofday(&tv, NULL);
        printf("[%ld.%06ld] ACK%ld sent, expecting packet%ld\n", tv.tv_sec, tv.tv_usec, recv_sequence-1, recv_sequence);
      }
      else
      {
        gettimeofday(&tv, NULL);
        printf("[%ld.%06ld] Packet%ld %c received\n", tv.tv_sec, tv.tv_usec, a, b);
        printf("Special Case, no ACKs will be sent\n");
      }
    }
  }


}

/* Timeout Event Handler */
void timer_handler()
{
  if(debug_mode == 1){printf("Handler Called\n");}

  // if(is_send)
  // {
  pthread_mutex_lock(&mutex);
  is_timeout = true;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&mutex);
  // }
  // else
  // {
  //   ;
  // }
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