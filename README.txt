# Instructions for compiling the program:

My submission should contain 13 files: README.txt, test.txt, Makefile, cnnode.c, cnnode.h, dvnode.c, dvnode.h, gbnnode.c, gbnnode.h, udp.c, udp.h, vector.c, and vector.h

Put all files in one folder under linux environment and type make. You should see the following:

cc -Wall -c gbnnode.c
cc -Wall -c udp.c
cc -Wall -o gbnnode gbnnode.o udp.o -lpthread
cc -Wall -c dvnode.c
cc -Wall -c vector.c
cc -Wall -o dvnode dvnode.o vector.o -lpthread
cc -Wall -c cnnode.c
cc -Wall -o cnnode cnnode.o vector.o -lpthread

Then you type ls and you should see three executables gbnnode, dvnode and cnnode respectively. Type make clean to clear the compiled object files and executables.

/////////////////////////////////////////////////////////////////////////////////

# Part 1: GBNNODE

# Implementation Overview

gbnnode runs on two threads: one on main() and another on network_thread_f(). The genearal outline is not too different from my submission on PA1: The main thread deals with user inputs and sending out messages, while the network thread is used to receive and process threads. So a message sent will go through main() of Program A to network() of Program B, then ACK is sent out from network thrad of Program B and received by the network() of Program A, which in turn notifies the main thread.

Sending/receiving sequence is implemented as long in this program. The sending buffer is fixed to be 2 times size of the sending window. Port number of the program is stored as self_port, which is a definition continued to be used in later programs. I put a hard cap on window size in gbnnode.h to be 10. I did not test sending cases which window size is beyond 10.

The GBN implemented here will send all unsent packets in the sliding window immediately if sliding window has been updated. This will cause an infinite loop between sender and receiver, if the sending window at the sender side equals to X, while the receiver is in deterministic mode that will drop every Xth packet. For instance: 

Sender: ./gbnnode 11451 19198 5 -d 5

Receiver: ./gbnnode 19198 11451 5 -d 5

And now if the sender sends a packet which is >= 2*5-1 = 9 characters the program will be stuck in a infinite loop.

This is because when ACK of 1st to 4th packets are received, my program immediates updates the window and send 6th to 9th packets straight away. On the receiver side, 5th packet will be the dropped packet and the new interval begins, then 6th-9th packet will come and act as the four undropped packets again, thus when 5th packet timed out and being sent again, it again becomes the fifth packet and thus needed to be dropped, creating an infinite loop.

P1 -> ACK1
P2 -> ACK2
P3 -> ACK3
P4 -> ACK4
P5 -> dropped
P6 -> ACK4
P7 -> ACK4 
P8 -> ACK4
P9 -> ACK4
P5(resend) -> dropped
P6 -> ACK4
P7 -> ACK4 
P8 -> ACK4
P9 -> ACK4

gbnnode calculates lost packets by count number of total received and successfully processed, which is different from what is implemnted in the cnnode, which counts the lost and total received instead.

# Files

gbnnode need four files to compile: gbnnode.h, gbnnode.c, udp.h, udp.c

gbnnode.c and gbnnode.h is the main program: two threads and process_command() and the actual implemenation of GBN protocol
udp.c and udp.h is old code recycled from PA1 that I used to set up UDP connections and send messages, it has been merged to a single file in dvnode and cnnode. 

# Messages

Messages are encoded in strictly 9 bytes per packet in the main body. with the first 8 bytes being the sequence number (long) and last byte being the character sent. ACK packets are implemented as packets with negative sequence number, a sequence number 0 is used to trigger summary page on the receiver side (it never knows when a transmission ends, so need to be triggered manually).

/////////////////////////////////////////////////////////////////////////////////

# Part 1: DVNODE

# Implementation Overview

dvnode runs on two threads: one on main() and one on network_thread_f(). The main thread is responsible for sending updates to its adjacent nodes while network thread is responsible for receiving and processing incoming messages. Network thread notifies the main thread to send updates by changing the value of flag_send.

The program flow is as follows:

1. Processing arguments and set up initial parameters

2. (Optional)Set up the program to run in background. I commented it out in my submission (Under /* Run the program in background */), but if uncomment it back one can run several instances on the same SSH window. Closing program can be done by "ps aux | grep dvnode" followed by "kill".

3. Bind UDP and network thread

4. Send starting message if program has keyword [last], else goes into the main loop

5. Network thread hears incoming message, update local distance vector, and notify main loop when to send packets to its neighbours.

The Distance Vector Algorithm is implemented at line 120-154 of devnode.c (commented // Distance Vector Algorithm in the first line)

# Files

dvnode.h dvnode.c is the main program.
vector.h vector.c is the distance vector and adjacency matrix (with weights), it's used in next section as well

# Datastructure

Both distance vector and adjacency matrix are implemented in linked list, despite a fixed maximum of 16 nodes. This is because I adapted the linked list structure (and some functions) from my code in PA1, in this way I can code a bit less. It is well defined and commented in vector.h: 

// Linked List Implementation of Distance Vector
struct distance_vector
{
    int node_num;                     // Destination vertices, in form of port number
    int next_hop;                     // Next hop if available
    float distance;                   // Edge weight
    struct distance_vector *next;     // Pointer to next
};

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


which send_sequence, prev_sequence and recv_sequence are planned to be used in the next part of this assignment. i.e. not used here.

# Messages

An entire distance vector is transferred in one UDP packet in form of (size) (int) (float) (int) (float)....etc. Below shows an example:

02 00 00 00 35 82 00 00 00 00 00 3F CE 56 00 00 CD CC 4C 3F

First four bytes (02 00 00 00) are a int which shows number of nodes (2) contained in this packet. Second four bytes 35 82 00 00 is the node number(33333), third four bytes (00 00 00 3F) are the float representing distance (0.5), fourth four bytes is the next node number(22222) and so on. With 16 nodes maximum one UDP packt can have at most 68 bytes to be sent.

If one want to see more details on the exact packets being sent and received simply increase value of debug_mode (=2 full info, =1 less info, =0 default) in dvnode.h.

/////////////////////////////////////////////////////////////////////////////////

# Part 3: CNNODE

# Implementation Overview

cnnode is combination of part 1 and part 2 in addition to a few modifications:

Now we have two edge_table linked list which is called head_et and head_p separately. head_et records the sender information (i.e. which nodes it can probe) and calculated loss rate is stored at the distance variable in the structure. head_p records the receiver information (i.e. when receving packets from this node what is the probability that this node drops the packet), it is also stored in the distance variable, head_p also stores all adjacent edges, i.e. when doing Distance Vector Updates the program choose edges from this linked list.

The main() runs on an interval of 5 seconds, on 1-4 seconds it sends probing packets to nodes which are specified by head_et. The amount of packets that can be sent per second is controlled as macro in cnnnode.c. At the 5th second (or interrupted by updated DV table) the system calculates the loss and do the DV update. Since the new loss rate can be higher, the previous DV table are erased to avoid storing untimely informations. An timer event is triggered every second for the implementation (timer2_handler())

# Files

cnnode.h cnnode.c is the main program.
vector.h vector.c is the distance vector and adjacency matrix (with weights), same file also used in dvnode

# Messages

Combining gbnnode and dvnode, an entire UDP packet in cnnode in form of (long) (char) (size) (int) (float) (int) (float)....etc. The (long) (char) is the same as in gbnnode while the (size) (int) (float) (int) (float) ...... is the same as in dvnode. Note that a probe/ACK packet does not have the (size) (int) (float) (int) (float) at the back.

If one want to see more details on the exact packets being sent and received simply change the debug_mode (=2 full info, =1 less info, =0 default) in cnnode.h. (printf that has %02X are the ones we are looking for)

////////////////////////////////////////////////////////////////////////////////////

Sample test cases are in a separate test.txt file


