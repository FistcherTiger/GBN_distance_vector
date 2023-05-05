
extern int debug_mode;                 // Debug_mode

extern pthread_t network_thread;       // Network thread
extern void *network_thread_f(void *); // Network thread

extern int self_port;                  // Port Number Used by This Program
extern int peer_port;                  // Port number of destination
int self_socket;                       // Socket Used by This Program

struct sockaddr_in server_addr;        // Socket address of the machine the program is running on
struct sockaddr_in client_addr;        // Socket addresses of incoming messages, here only peer is possible

extern char input[];
extern int window_size;
extern long send_sequence;
extern long recv_sequence;
extern long prev_sequence;

void initialize_udp();                 // UDP initialization

void send_msg(int self_socket, char buffer, long send_sequence, struct sockaddr_in destaddr);                       // Sending message using UDP

extern struct itimerval timer;
void timer_handler();                  // Event handler for timeout
