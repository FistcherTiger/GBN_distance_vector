/* Threads */
pthread_t network_thread;
void *network_thread_f(void *);

/* Timers */
struct itimerval timer;
void timer_handler();
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

/* UDP Related */
int self_port = -1;                                     // Port number of this machine
int peer_port = -1;                                     // Port number of peer machine
int self_socket;                                        // Socket Used by This Program
struct sockaddr_in server_addr;                         // Socket address of the machine the program is running on
struct sockaddr_in client_addr;                         // Socket addresses of incoming messages
void initialize_udp();                                  // UDP initialization
void send_msg(int self_socket, long send_sequence, char header, char *buffer, struct sockaddr_in destaddr); // Sending message using UDP
void send_dv_all();                                     // Send updated distance vector to all
char *dv_to_buf();                                      // Convert dv structure to sendable string
struct distance_vector *buf_to_dv(char *buffer);        // Convert string to dv structure;
void timer_handler();                                   // Event handler for timeout

/* Global variables*/
int debug_mode = 0;
int window_size = -1;                                   // Window size used in GBN protocol
double p_probability = -1.0;                            // Chane of faliure in probabilistic mode