/* Macros */
#define MAX_WINDOW_SIZE 10             // Maximum window size allowed
#define MAX_INTERVAL 99999             // Maximum interval for loss packts in deterministic mode


/* Threads */
pthread_t network_thread;
void *network_thread_f(void *);

/* Timers */
struct itimerval timer;
void timer_handler();
//bool is_sent;                          // Flag to signal whether the timer is being used by send-system
bool is_timeout;                       // Flag for timeout
int is_ack;                            // Flag for ack, Update from 0 whenever the sending window has moved, value indicates how long it moves

/* Timestamp */
struct timeval tv;

/* Helper Functions */
int process_arguments(int argc, char *argv[]);          // Process input arguments
int str_to_portnum(char *str);                          // Convert string to port number
int str_to_window(char *str);                           // Convert string to window size
int str_to_interval(char *str);                         // Convert string to interval in deterministic mode
double str_to_prob(char *str);                          // Convert string to probability in probabilistic mode
void process_command(char *command);                    // Process argument commands
void recv_GBN(char *buffer, long a, char b);            // Receive function under GBN protocol
void move_window(int x);                                // Move window x times, one step at a time

/* Global variables*/
int debug_mode = 0;

int self_port = -1;                                     // Port number of this machine
int peer_port = -1;                                     // Port number of peer machine
int window_size = -1;                                   // Window size used in GBN protocol
int d_interval = -1;                                    // Interval size in deterministic mode
double p_probability = -1.0;                            // Chane of faliure in probabilistic mode

long send_sequence = 1UL;
long recv_sequence = 1UL;
long prev_sequence = 1UL;