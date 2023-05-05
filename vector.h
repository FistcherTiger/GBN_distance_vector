extern int debug_mode;                 // Debug_mode

/* Timestamp */
extern struct timeval tv;

// Linked List Implementation of Distance Vector
struct distance_vector
{
    int node_num;                     // Destination vertices, in form of port number
    int next_hop;                     // Next hop if available
    float distance;                   // Edge weight
    struct distance_vector *next;     // Pointer to next
};

struct distance_vector *create_vector(int node, float dis);                            // Create New Vector
struct distance_vector *find_vector(struct distance_vector *head, int node);           // Find Vector by node_num, unique
void insert_vector(struct distance_vector **head, struct distance_vector *u);          // Insert Vector in the first place
void change_vector(struct distance_vector *u, float dis, int hop);                     // Change value of vector
void remove_vector(struct distance_vector **head, int node);                           // Remove a distance vector by node name
void destroy_list(struct distance_vector **head);                                      // Destory an entire dv list
char *print_vector(struct distance_vector *u);
char *print_vector_list(struct distance_vector *head);

// Linked List Implementation of Edge table
struct edge_table
{
    int node_num;                     // Destination vertices, in form of port number
    float distance;                   // Edge weight
    long send_seq;                    // Same as in GBN protocol. send sequence
    long recv_seq;                    // recv_sequence
    long prev_seq;                    // prev_sequence
    long total_ct;                    // count_total
    long faliure_ct;                  // count_total - count_success
    struct edge_table *next;          // Pointer to next
};

struct edge_table *create_edge(int node, float dis);
struct edge_table *find_edge(struct edge_table *head, int node);
void insert_edge(struct edge_table **head, struct edge_table *u);
void print_edge(struct edge_table *head);
