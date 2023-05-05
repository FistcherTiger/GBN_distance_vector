#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vector.h"

/* Distance Vector */

// Create a node
struct distance_vector *create_vector(int node, float dis)
{
  struct distance_vector *u = malloc(sizeof(struct distance_vector));
  u->node_num = node;
  u->distance = dis;
  u->next_hop = node;
  u->next = NULL;
  
  return u;
}

// Loop Through the node to find an element, by node_num, return null if not found
struct distance_vector *find_vector(struct distance_vector *head, int node)
{
  struct distance_vector *curr;
  for (curr = head; curr != NULL; curr = curr->next) 
  {
    if (curr->node_num == node)
    {
      return curr;
    }
  }
  return NULL;
}

// Loop through the node to remvoe an element, by int_node
void remove_vector(struct distance_vector **head, int node)
{
  if(head==NULL)
  {
    return;
  }

  struct distance_vector *prev = NULL;
  struct distance_vector *curr;

  for (curr = *head; curr != NULL; prev = curr, curr = curr->next) 
  {
    if (curr->node_num == node) 
    {
      if (prev == NULL) 
      {
        *head = curr->next;
      } 
      else
      {
        prev->next = curr->next;
      }
      free(curr);
      return;
    }
  }
  return;
}

// Remove entire list, used when udpating clients
void destroy_list(struct distance_vector **head)
{
  struct distance_vector *curr = *head;

  while (curr != NULL) 
  {
    struct distance_vector *next = curr->next;
    free(curr);
    curr = next;
  }
  *head = NULL;
  return;
}


// Pop vector at first position
void insert_vector(struct distance_vector **head, struct distance_vector *u)
{
  u->next = *head;
  *head = u;
}

// Change a vector value by overwriting, find by node_num
void change_vector(struct distance_vector *u, float dis, int hop) 
{
  if (u != NULL) 
  {
    u->distance = dis;
    u->next_hop = hop;
  }
}

// Print One vector, return the string
char *print_vector(struct distance_vector *u)
{
  char *output = malloc(128);
  if (u->next_hop != u->node_num)
  {
    sprintf(output, " - (%.2f) -> Node %d ; Next hop -> Node %d",u->distance, u->node_num, u->next_hop);
  }
  else
  {
    sprintf(output, " - (%.2f) -> Node %d ",u->distance, u->node_num);
  }
  return output;
}

// Print entire list
char *print_vector_list(struct distance_vector *head)
{
  struct distance_vector *curr;
  char *print_temp = malloc(1024);
  memset(print_temp, 0, 1024);  

  for (curr = head; curr != NULL; curr = curr->next) 
  {
    char curr_print[1024];
    sprintf(curr_print, "%s\n", print_vector(curr));  // format the output of print_vector and store it in curr_print
    strcat(print_temp, curr_print);
  }

  return print_temp;  // return the concatenated string, print separately will be interrupted by e.g. incoming messages
}

/* Routing Table */

// Create an edge
struct edge_table *create_edge(int node, float dis)
{
  struct edge_table *u = malloc(sizeof(struct edge_table));
  u->node_num = node;
  u->distance = dis;
  u->send_seq = 1UL;               // Same as in GBN protocol
  u->recv_seq = 1UL;
  u->prev_seq = 1UL;
  u->total_ct = 1UL;               // Guarantee no divide by zero happens, 1 becomes small as total count becomes big
  u->faliure_ct = 0UL;
  u->next = NULL;
  
  return u;
}


// Loop Through the edge to find an edge, by node_num
struct edge_table *find_edge(struct edge_table *head, int node)
{
  struct edge_table *curr;
  for (curr = head; curr != NULL; curr = curr->next) 
  {
    if (curr->node_num == node)
    {
      return curr;
    }
  }

  return NULL;
}

// Pop at First position
void insert_edge(struct edge_table **head, struct edge_table *u)
{
    u->next = *head;
    *head = u;
}

// Print all avail routes
void print_edge(struct edge_table *head)
{
  struct edge_table *curr;
  for (curr = head; curr != NULL; curr = curr->next) 
  {
    printf("Edge Num: %d, Distance: %.2f\n", curr->node_num, curr->distance);
    fflush(stdout);
  }
}

/* Used for Debugging in separate files */
/////////////////////////////////////////////////////////////////
// int main()
// {
//   struct distance_vector *head = NULL;
//   insert_vector(&head, create_vector(10001,0.1));
//   insert_vector(&head, create_vector(10002,0.2));
//   insert_vector(&head, create_vector(10003,0.3));
//   insert_vector(&head, create_vector(10006,0.6));
//   insert_vector(&head, create_vector(10007,0.1));
//   insert_vector(&head, create_vector(10008,0.8));
//   print_vector_list(head);

//   printf("Phase 2\n");

//   struct distance_vector *s = find_vector(head,10006);
//   printf("%s\n",print_vector(s));

//   change_vector(s,0.99,20000);

//   printf("%s\n",print_vector(s));

//   struct edge_table *head_r = NULL;
//   insert_edge(&head_r, create_edge(20001, 0.5));
//   insert_edge(&head_r, create_edge(20002, 0.1));
//   insert_edge(&head_r, create_edge(20157, 0.6));
//   insert_edge(&head_r, create_edge(20008, 0.25));
//   print_edge(head_r);

//   return 0;
// }
////////////////////////////////////////////////////////////////
