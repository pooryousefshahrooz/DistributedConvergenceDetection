/* BGP packet management routine.
   Copyright (C) 1999 Kunihiro Ishiguro

This file is part of GNU Zebra.

GNU Zebra is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

GNU Zebra is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Zebra; see the file COPYING.  If not, write to the Free
Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

#include <zebra.h>

#include "thread.h"
#include "stream.h"
#include "network.h"
#include "prefix.h"
#include "command.h"
#include "log.h"
#include "memory.h"
#include "sockunion.h"		/* for inet_ntop () */
#include "sockopt.h"
#include "linklist.h"
#include "plist.h"
#include "filter.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_dump.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_open.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_community.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_network.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_encap.h"
#include "bgpd/bgp_advertise.h"
#include "bgpd/bgp_vty.h"
/* A key/value dict system for our root cause events */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string.h>




// extern struct peer *sent_peers;
// extern struct peer *to_be_sent_peers;
// extern struct peer *waiting_peers;
// extern uint32_t rec_time_stamp;
// extern uint32_t rec_root_cause_event_id;
// extern uint32_t rec_root_cause_event_owner_router_id;
// extern uint32_t root_cause_event_owner;


extern struct peer *a_peer_for_maintating_head_of_data_structure;

// extern struct peer_list* head_for_peer_list;




int stream_put_prefix (struct stream *, struct prefix *);

/* Set up BGP packet marker and packet type. */
static int
bgp_packet_set_marker (struct stream *s, u_char type)
{
  int i;

// // I added this 2 bytes new word to the beginning of the streem
//   stream_putw (s, 0);
  /* Fill in marker. */
  for (i = 0; i < BGP_MARKER_SIZE; i++)
    stream_putc (s, 0xff);

  /* Dummy total length. This field is should be filled in later on. */
  stream_putw (s, 0);

  /* BGP packet type. */
  stream_putc (s, type);

  // if we add our root cause event information here, we need to change the reading pinter after the type char




  /* Return current stream size. */
  return stream_get_endp (s);
}

/* Set BGP packet header size entry.  If size is zero then use current
   stream size. */
static int
bgp_packet_set_size (struct stream *s)
{
  int cp;

  /* Preserve current pointer. */
  cp = stream_get_endp (s);
  stream_putw_at (s, BGP_MARKER_SIZE, cp);

  return cp;
}

/* Add new packet to the peer. */
static void
bgp_packet_add (struct peer *peer, struct stream *s)
{
  /* Add packet to the end of list. */
  stream_fifo_push (peer->obuf, s);
}

/* Free first packet. */
static void
bgp_packet_delete (struct peer *peer)
{
  stream_free (stream_fifo_pop (peer->obuf));
}

/* Check file descriptor whether connect is established. */
static void
bgp_connect_check (struct peer *peer)
{
  int status;
  socklen_t slen;
  int ret;

  /* Anyway I have to reset read and write thread. */
  BGP_READ_OFF (peer->t_read);
  BGP_WRITE_OFF (peer->t_write);

  /* Check file descriptor. */
  slen = sizeof (status);
  ret = getsockopt(peer->fd, SOL_SOCKET, SO_ERROR, (void *) &status, &slen);

  /* If getsockopt is fail, this is fatal error. */
  if (ret < 0)
    {
      zlog (peer->log, LOG_INFO, "can't get sockopt for nonblocking connect");
      BGP_EVENT_ADD (peer, TCP_fatal_error);
      return;
    }      

  /* When status is 0 then TCP connection is established. */
  if (status == 0)
    {
      BGP_EVENT_ADD (peer, TCP_connection_open);
    }
  else
    {
      if (BGP_DEBUG (events, EVENTS))
	  plog_debug (peer->log, "%s [Event] Connect failed (%s)",
		     peer->host, safe_strerror (errno));
      BGP_EVENT_ADD (peer, TCP_connection_open_failed);
    }
}



/* Make BGP_MSG_CONVERGENCE packet and send it to the peer. */

void
bgp_convergence_send (struct peer *peer,uint32_t *passed_root_cause_event_id,char *prefix)
{
  //zlog_debug ("We are going to make a BGP_MSG_CONVERGENCE message to send to %s",peer->host);
  zlog_debug ("We are going to send a BGP_MSG_CONVERGENCE message to  %s and he parameters are E_id is %ld , and prefix %s ",peer->host,passed_root_cause_event_id,prefix);
  //zlog_debug(" lets convert prefix (%s)to four numbers to send %s", prefix);
  char my_delim[]= ".";
  char *my_ptr = strtok(prefix, my_delim);
  int my_ip_array[4];
  int i=0;

  while(my_ptr != NULL)
  {
      my_ip_array[i] =atoi(my_ptr);
      i = i+1;
      my_ptr = strtok(NULL, my_delim);
  }

  afi_t afi;
  safi_t safi;

  struct stream *s;
  struct stream *snlri;
  struct bgp_adj_out *adj;
  struct bgp_advertise *adv;
  struct stream *packet;
  struct bgp_node *rn = NULL;
  struct bgp_info *binfo = NULL;
  bgp_size_t total_attr_len = 0;
  unsigned long attrlen_pos = 0;
  int space_remaining = 0;
  int space_needed = 0;
  size_t mpattrlen_pos = 0;
  size_t mpattr_pos = 0;
  s = peer->work;
  stream_reset (s);
  snlri = peer->scratch;
  stream_reset (snlri);

      /* 1: Write the BGP message header - 16 bytes marker, 2 bytes length,
     * one byte message type.
     */
  adv = BGP_ADV_FIFO_HEAD (&peer->sync[afi][safi]->update);


  //zlog_debug ("%s We are before the while (adv)", ".......................");

  s = stream_new (BGP_MAX_PACKET_SIZE);
  bgp_packet_set_marker (s, BGP_MSG_CONVERGENCE);

 stream_putl (s, passed_root_cause_event_id);

 stream_putl (s, my_ip_array[0]);
 stream_putl (s, my_ip_array[1]);
 stream_putl (s, my_ip_array[2]);
 stream_putl (s, my_ip_array[3]);
 
  /* 2: withdrawn routes length */
  stream_putw (s, 0);


  /* 3: total attributes length - attrlen_pos stores the position */
  attrlen_pos = stream_get_endp (s);

  stream_putw (s, 0);
  stream_putl (s, 1);
  stream_putl (s, 1);
  stream_putl (s, 1);
  stream_putl (s, 1);
  stream_putl (s, 1);
  stream_putl (s, 1);
  stream_putl (s, 1);
  stream_putl (s, 1);

  mpattr_pos = stream_get_endp(s);
  //zlog_debug ("%s We are after  the while (adv)", ".....---------..................");
   int length;
  length = strlen("shahroozshahrooz");

  total_attr_len = 32;
  packet = stream_dup (s);
  bgp_packet_set_size (packet);
  bgp_packet_add (peer, packet);
  BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
  stream_reset (s);
  stream_reset (snlri);
  //zlog_debug ("%s We are at the returning packet point for sending convergence message", ".....---------..................");
  
  return packet;


  return NULL;










  //   afi_t afi;
  // safi_t safi;


  // struct stream *s;
  // struct stream *snlri;
  // struct bgp_adj_out *adj;
  // struct bgp_advertise *adv;
  // struct stream *packet;
  // struct bgp_node *rn = NULL;
  // struct bgp_info *binfo = NULL;
  // bgp_size_t total_attr_len = 0;
  // unsigned long attrlen_pos = 0;
  // int space_remaining = 0;
  // int space_needed = 0;
  // size_t mpattrlen_pos = 0;
  // size_t mpattr_pos = 0;

  // s = peer->work;
  // stream_reset (s);
  // snlri = peer->scratch;
  // stream_reset (snlri);

  //     /* 1: Write the BGP message header - 16 bytes marker, 2 bytes length,
  //    * one byte message type.
  //    */
  // adv = BGP_ADV_FIFO_HEAD (&peer->sync[afi][safi]->update);


  // //zlog_debug ("%s We are before the while (adv)", ".......................");

  // s = stream_new (BGP_MAX_PACKET_SIZE);
  // bgp_packet_set_marker (s, BGP_MSG_CONVERGENCE);

  //   //  int length;
  //   // length = strlen("shahroozshahrooz");

  //   // char result[50]; 
  //   // sprintf(result, "%u", length); 

  //   //   zlog_debug ("%s this is shahrooz's lenghth", result);

  //   /* 2: Write timestamp */
  //  //stream_putl (s, passed_time_stamp);

  //   /* 2: Write root cause event ID */
  //    stream_putl (s, passed_root_cause_event_id);

  //     char my_delim[]= ".";

  //     char *my_ptr = strtok(prefix, my_delim);
  //     int my_ip_array[4];
  //     int i=0;

  //     while(my_ptr != NULL)
  //     {
  //         my_ip_array[i] =atoi(my_ptr);
  //         i = i+1;
  //         my_ptr = strtok(NULL, my_delim);
  //     }

  //     // zlog_debug("%d ", my_ip_array[0]);
  //     // zlog_debug("%d ", my_ip_array[1]);
  //     // zlog_debug("%d ", my_ip_array[2]);
  //     // zlog_debug("%d ", my_ip_array[3]);

  //    stream_putl (s, my_ip_array[0]);

  //   stream_putl (s, my_ip_array[1]);
  //   stream_putl (s, my_ip_array[2]);
  //   stream_putl (s, my_ip_array[3]);

  //   //stream_putl (s, passed_router_id);

  //   /* 2: withdrawn routes length */
  //   stream_putw (s, 0);

  //   /* 3: total attributes length - attrlen_pos stores the position */
  //   attrlen_pos = stream_get_endp (s);

  //   stream_putw (s, 0);
  //   mpattr_pos = stream_get_endp(s);
  //   packet = stream_dup (s);
  //   bgp_packet_set_size (packet);
  //   bgp_packet_add (peer, packet);
  //   BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
  //   stream_reset (s);
  //   stream_reset (snlri);
  //   //zlog_debug ("%s We are at the returning packet point for sending FIZZLE", ".....---------..................");

  //   return packet;
  // return NULL;


}

/* Make BGP_MSG_FIZZLE packet and send it to the peer. */

void
bgp_fizzle_send (struct peer *peer,uint32_t *passed_root_cause_event_id,char *passed_time_stamp,uint32_t *passed_root_cause_event_owner_router_id,char *passed_prefix, uint32_t *E_owner_id)
{

 




zlog_debug ("We are going to send a BGP_MSG_FIZZLE message to %s and the parameters are time_stamp is %s,E_id is %ld , and router_owner is %ld ",peer->host,passed_time_stamp,passed_root_cause_event_id,E_owner_id);
//zlog_debug ("We need to convert time stamp to int and send in putl in stream");
uint32_t my_converted_time_stamp ;
my_converted_time_stamp = (int)passed_time_stamp;

int my_converted_time_stamp2 = (int)strtol(passed_time_stamp, (char **)NULL, 10); // strtol = STRing TO Long

zlog_debug ("We converted");
// zlog_debug ("We converted and the value of my_converted_time_stamp  is %u",my_converted_time_stamp);
// zlog_debug ("We converted and the value  of my_converted_time_stamp2 is %ld",my_converted_time_stamp2);
// zlog_debug ("We converted and the value of my_converted_time_stamp2 is %ld",my_converted_time_stamp2);

// zlog_debug ("We converted and the value of my_converted_time_stamp2 is %d",my_converted_time_stamp2);







// zlog (peer->log, LOG_DEBUG, "%s rcvd %s/%d",
//       peer->host,
//       inet_ntop(p->family, &p->u.prefix, buf, SU_ADDRSTRLEN),
//       p->prefixlen);

    //zlog_debug(" lets convert prefix (%s)to four numbers to send %s", passed_prefix);
    char my_delim[]= ".";

    char *my_ptr = strtok(passed_prefix, my_delim);
    int my_ip_array[4];
    int i=0;

    while(my_ptr != NULL)
    {
        my_ip_array[i] =atoi(my_ptr);
        i = i+1;
        my_ptr = strtok(NULL, my_delim);
    }

    // zlog_debug("%d ", my_ip_array[0]);
    // zlog_debug("%d ", my_ip_array[1]);
    // zlog_debug("%d ", my_ip_array[2]);
    // zlog_debug("%d ", my_ip_array[3]);



  // printf("%d \n", ip_array[0]);
  // printf("%d \n", ip_array[1]);
  // printf("%d \n", ip_array[2]);
  // printf("%d \n", ip_array[3]);




    afi_t afi;
  safi_t safi;


  struct stream *s;
  struct stream *snlri;
  struct bgp_adj_out *adj;
  struct bgp_advertise *adv;
  struct stream *packet;
  struct bgp_node *rn = NULL;
  struct bgp_info *binfo = NULL;
  bgp_size_t total_attr_len = 0;
  unsigned long attrlen_pos = 0;
  int space_remaining = 0;
  int space_needed = 0;
  size_t mpattrlen_pos = 0;
  size_t mpattr_pos = 0;

  s = peer->work;
  stream_reset (s);
  snlri = peer->scratch;
  stream_reset (snlri);

      /* 1: Write the BGP message header - 16 bytes marker, 2 bytes length,
     * one byte message type.
     */
  adv = BGP_ADV_FIFO_HEAD (&peer->sync[afi][safi]->update);


  //zlog_debug ("%s We are before the while (adv)", ".......................");

  s = stream_new (BGP_MAX_PACKET_SIZE);
  bgp_packet_set_marker (s, BGP_MSG_FIZZLE);

    //  int length;
    // length = strlen("shahroozshahrooz");

    // char result[50]; 
    // sprintf(result, "%u", length); 

    //   zlog_debug ("%s this is shahrooz's lenghth", result);

    /* 2: Write timestamp */
   stream_putl (s, my_converted_time_stamp2);

    /* 2: Write root cause event ID */
   stream_putl (s, passed_root_cause_event_id);

   
   stream_putl (s, E_owner_id);

   stream_putl (s, my_ip_array[0]);

   stream_putl (s, my_ip_array[1]);
   stream_putl (s, my_ip_array[2]);
  stream_putl (s, my_ip_array[3]);
 
    /* 2: withdrawn routes length */
    stream_putw (s, 0);


    /* 3: total attributes length - attrlen_pos stores the position */
    attrlen_pos = stream_get_endp (s);

    stream_putw (s, 0);


    stream_putl (s, 1);
    stream_putl (s, 1);
    stream_putl (s, 1);
    stream_putl (s, 1);
    stream_putl (s, 1);
    stream_putl (s, 1);
    stream_putl (s, 1);
    stream_putl (s, 1);
    /* 4: if there is MP_REACH_NLRI attribute, that should be the first
     * attribute, according to draft-ietf-idr-error-handling. Save the
     * position.
     */
    mpattr_pos = stream_get_endp(s);
    //zlog_debug ("%s We are after  the while (adv)", ".....---------..................");


     int length;
 
  
 
    length = strlen("shahroozshahrooz");


    total_attr_len = 32;
      /* set the total attribute length correctly */
    //stream_putw_at (s, attrlen_pos, total_attr_len);


  /////
    packet = stream_dup (s);
    bgp_packet_set_size (packet);
    bgp_packet_add (peer, packet);
    BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
    stream_reset (s);
    stream_reset (snlri);
    //zlog_debug ("%s We are at the returning packet point for sending FIZZLE", ".....---------..................");
    
    return packet;
  
    



  return NULL;


}




/* Make BGP update packet.  */
static struct stream *
bgp_update_packet (struct peer *peer, afi_t afi, safi_t safi)
{



   uint32_t  generated_time_stamp = 12345;


    /* 2: Write root cause event ID */
   uint32_t generated_root_cause_id =12345;
   uint32_t root_cause_event_owner;
   bool we_have_received_prefix = false;



  char buf[SU_ADDRSTRLEN];


  zlog_debug ("We are going to get time stamp and root cause event id from one of the prefixes we are going to send to %s", "..................");




    // struct peer * check_peer = (struct peer *) malloc(sizeof(struct peer));
    // check_peer -> local_as = 4545;
    // check_peer -> host = "host 4545";
    // add_to_prefix_neighbour_pair(&(a_peer_for_maintating_head_of_data_structure -> pref_neigh_pair ), "44.33.22.11", check_peer);

  struct bgp *bgp;

  bgp = bgp_get_default ();
  u_int32_t as_number;
  as_number = bgp->as;

  char as_number_str[10];
 
   
  sprintf( as_number_str, "%u", as_number );

  struct peer *head_peer;

    
    /* Allocate new peer. */
    head_peer = XCALLOC (MTYPE_BGP_PEER, sizeof (struct peer));


  //zlog_debug ("this is our peer->local_as %s",as_number_str);



  struct stream *s;
  struct stream *snlri;
  struct bgp_adj_out *adj;
  struct bgp_advertise *adv;
  struct stream *packet;
  struct bgp_node *rn = NULL;
  struct bgp_info *binfo = NULL;
  bgp_size_t total_attr_len = 0;
  unsigned long attrlen_pos = 0;
  int space_remaining = 0;
  int space_needed = 0;
  size_t mpattrlen_pos = 0;
  size_t mpattr_pos = 0;

  s = peer->work;
  stream_reset (s);
  snlri = peer->scratch;
  stream_reset (snlri);

  adv = BGP_ADV_FIFO_HEAD (&peer->sync[afi][safi]->update);


  //zlog_debug ("%s We are before the while (adv)", ".......................");

  while (adv)
    {
      assert (adv->rn);
      rn = adv->rn;
      adj = adv->adj;
      if (adv->binfo)
        binfo = adv->binfo;

      space_remaining = STREAM_CONCAT_REMAIN (s, snlri, STREAM_SIZE(s)) -
                        BGP_MAX_PACKET_SIZE_OVERFLOW;
      space_needed = BGP_NLRI_LENGTH + bgp_packet_mpattr_prefix_size (afi, safi, &rn->p);

      /* When remaining space can't include NLRI and it's length.  */
      if (space_remaining < space_needed)
	break;

      /* If packet is empty, set attribute. */
      if (stream_empty (s))
	{
	  struct prefix_rd *prd = NULL;
	  u_char *tag = NULL;
	  struct peer *from = NULL;

	  if (rn->prn)
	    prd = (struct prefix_rd *) &rn->prn->p;
          if (binfo)
            {
              from = binfo->peer;
              if (binfo->extra)
                tag = binfo->extra->tag;
            }

	  /* 1: Write the BGP message header - 16 bytes marker, 2 bytes length,
	   * one byte message type.
	   */
	  bgp_packet_set_marker (s, BGP_MSG_UPDATE);

    //  int length;
 
  
 
    // length = strlen("shahroozshahrooz");

    // char result[50]; 
    // sprintf(result, "%u", length); 



    //zlog_debug ("!!!!!!!!!!!!!!!!!!!!!!!!******!!!!! this is %s prefix value in while adv", );



      struct peer_list* test_peer_list = NULL;
      add_to_peer_list(&(test_peer_list), peer);
     
      zlog_debug ("+++++++++++++++++++++++++++++++++  we are going to add and print the list of neighbors we will send global convergence of (%s) to ++++++++++++++++++++","X");


      //add_to_neighbours_of_a_prefix(&(peer -> neighbours_of_prefix), "6.6.6.6", test_peer_list);
      add_to_neighbours_of_a_prefix(&(a_peer_for_maintating_head_of_data_structure -> neighbours_of_prefix), inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ), test_peer_list);
       
    //zlog_debug(".............I am in a complex loop for %s..............",inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ));

    struct prefix_neighbour_pair * best_received_neighbor = (struct prefix_neighbour_pair *) malloc(sizeof(struct prefix_neighbour_pair));
    //zlog_debug ("lets call get_from_prefix_neighbour_pair");

    //a_peer_for_maintating_head_of_data_structure -> pref_neigh_pair = NULL;
    best_received_neighbor = get_from_prefix_neighbour_pair(&(a_peer_for_maintating_head_of_data_structure -> pref_neigh_pair), inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ));
    if (best_received_neighbor != NULL)
    {
      we_have_received_prefix = true;
    zlog_debug("this is the as: %ld  and host: %s of the peer that I have received prefix %s  from ", best_received_neighbor -> val_peer -> as, best_received_neighbor -> val_peer -> host,inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ));

    struct received_prefix * res_rec_pref = (struct received_prefix *) malloc(sizeof(struct received_prefix));
    
    res_rec_pref = get_from_received_prefix(&(a_peer_for_maintating_head_of_data_structure -> received_prefix), inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ), best_received_neighbor );
    // struct received_prefix * res_rec_pref8 = (struct received_prefix *) malloc(sizeof(struct received_prefix));
    // res_rec_pref8 = get_from_received_prefix(&(a_peer_for_maintating_head_of_data_structure -> received_prefix), inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ), best_received_neighbor );
    //zlog_debug("check: lets test the peer->host of saved  %s ",best_received_neighbor->val_peer->host);
    //zlog_debug("check: the extracted peers' time stamp is  %s ",res_rec_pref -> time_stamp);

    char *my_time_stamp[50];
    strncpy(my_time_stamp, res_rec_pref -> time_stamp, 50);
    //my_time_stamp= res_rec_pref -> time_stamp;
    // zlog_debug("check: my_time_stamp value is %s  ",my_time_stamp);

    // //zlog_debug("test: the extracted peers' time stamp.time_stamp is  %s ",res_rec_pref.time_stamp);
    // zlog_debug("check: We extracted this for prefix %s ",res_rec_pref->prefix_received);
    // zlog_debug("check: We extracted this with time stamp %s ",res_rec_pref -> time_stamp);
    // zlog_debug("check: We extracted this with E_id %d ",res_rec_pref -> event_id);

    // zlog_debug("check: my_time_stamp value is %s  ",my_time_stamp);

    // zlog_debug("check: my_time_stamp value is %s  ",my_time_stamp);


    // int g =12;
    // g = g+2;
    //zlog_debug("check: my_time_stamp value is %s  ",my_time_stamp);







    // struct received_prefix * res_rec_pref72 = (struct received_prefix *) malloc(sizeof(struct received_prefix));

    // res_rec_pref72 = get_from_received_prefix(&(a_peer_for_maintating_head_of_data_structure -> received_prefix), inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ), peer );
    // zlog_debug("added in route.c: the extracted peers' time stamp is  %s ",res_rec_pref72 -> time_stamp);
    // zlog_debug("added in route.c: We extracted this for prefix %s ",res_rec_pref72->prefix_received);
    // zlog_debug("added in route.c: We extracted this with time stamp %s ",res_rec_pref72 -> time_stamp);
    // zlog_debug("added in route.c: We extracted this with E_id %d ",res_rec_pref72 -> event_id);






    if (res_rec_pref!= NULL)
      {

      // generate time stamp
      struct timeval te2; 
      gettimeofday(&te2, NULL); // get current time
      //long gen_time_stamp = te2.tv_sec*1000LL + te2.tv_usec/1000; // calculate milliseconds

      uint32_t gen_time_stamp = 100 + rand() / (RAND_MAX / (1000 - 100 + 1) + 1);
      //gen_time_stamp = generated_time_stamp2;

      // printf("milliseconds: %lld\n", milliseconds);
      //return milliseconds;
      char char_milliseconds2[50]; 
      sprintf(char_milliseconds2, "%u", gen_time_stamp); 



      addcause(&(a_peer_for_maintating_head_of_data_structure -> cause),char_milliseconds2,"CBGPMSG",res_rec_pref -> event_id, peer->as,inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ), "1 2 3", res_rec_pref -> time_stamp,res_rec_pref->peer_received_from);

      add_to_sent(&(a_peer_for_maintating_head_of_data_structure -> sent),char_milliseconds2,peer,res_rec_pref -> event_id , inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ));



      zlog_debug("check: my_time_stamp value is %s  ",my_time_stamp);


      //addcause(struct cause ** head_ref,                               char in_time_stamp, char* in_message_type, uint32_t in_event_id,uint32_t in_router_id, char* in_prefix_str,                  char* in_as_path, char in_received_timestamp, struct peer* in_neighbour );


      char char_generated_time_stamp[50]; 
      sprintf(char_generated_time_stamp, "%u", generated_time_stamp); 


     zlog_debug("check: my_time_stamp value is %s  ",my_time_stamp);


      //zlog_debug("we converted our time stamp to int");
      zlog_debug("**************************   the cause of %s with time stamp %s sent to %s is time stamp  %s received from %s  and in_neighbor to send back is %s",inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ),char_milliseconds2,peer->host,my_time_stamp,res_rec_pref->peer_received_from->host,res_rec_pref->peer_received_from->host);
      //zlog_debug("lets add a default cause value ");
      //addcause(&(a_peer_for_maintating_head_of_data_structure -> cause),"123","CBGPMSG",6765, 3434,"10.68.62.5", "1 2 3", time(NULL),peer);
      
      //zlog_debug("lets add and print sent data structure");
      //add_to_sent(&(a_peer_for_maintating_head_of_data_structure -> sent),generated_time_stamp,peer, peer->as, inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ));
      //add_to_sent(struct sent** head_ref, char * in_time_stamp, struct peer* in_neighbour, uint32_t in_router_id, char * in_prefix);
      //print_sent(&(a_peer_for_maintating_head_of_data_structure -> sent));
      //zlog_debug("We added and printed sent data structure");
      //print_cause(&(a_peer_for_maintating_head_of_data_structure -> cause));
     // zlog_debug("lets add our own cause value ");
     // addcause(&(a_peer_for_maintating_head_of_data_structure -> cause),generated_time_stamp,"CBGPMSG",generated_root_cause_id, peer->as,inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ), "1 2 3", "NULL",peer);
      //struct cause* temp_cause = (struct cause*) malloc(sizeof(struct cause));
      //temp_cause = getcause(&(a_peer_for_maintating_head_of_data_structure -> cause), generated_time_stamp);

      //zlog_debug("this is the ip:%s of cause entry with time stamp %ld", temp_cause -> prefix_str, generated_time_stamp);
      zlog_debug("**************************   We are sending %s with time stamp %s  to %s **********",inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ),char_milliseconds2,peer->host);

      zlog_debug("check: my_time_stamp value is %s  ",my_time_stamp);


      stream_putl (s, gen_time_stamp);
        /* 2: Write root cause event ID */
      stream_putl (s, res_rec_pref -> event_id);

      //root_cause_event_owner = peer->as;

      stream_putl (s,res_rec_pref->received_E_owner_id );

      zlog_debug("check: my_time_stamp value is %s  ",my_time_stamp);


      }
      else{
            zlog_debug("We did not get anything peer for receiving prefix %s ",inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ));

            print_prefix_neighbour_pair(&(a_peer_for_maintating_head_of_data_structure -> pref_neigh_pair));
            //return NULL;
          }

        }
        else
        {
          zlog_debug("We did not get anything for best peer for prefix %s lets return but we do not return!",inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ));

          print_prefix_neighbour_pair(&(a_peer_for_maintating_head_of_data_structure -> pref_neigh_pair));
          //return NULL;
        }

  if (we_have_received_prefix != true)// we have not received any update so we are the owner of event,create GRCMSG
        {
      zlog_debug("******************");
      zlog_debug("********* .... we are the owner of this event *********");

      zlog_debug("**********.....lets generate time stamp,E-ID and  add them to cause ans sent...********");

      // generate time stamp
      struct timeval te; 
      gettimeofday(&te, NULL); // get current time
      //long time_stamp_generated = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
      uint32_t time_stamp_generated = 100 + rand() / (RAND_MAX / (1000 - 100 + 1) + 1);
       //time_stamp_generated = generated_time_stamp;
      // printf("milliseconds: %lld\n", milliseconds);
      //return milliseconds;
      char char_time_stamp[50]; 
      char char_time_stamp_router_id[50]; 
      char char_version_time_stamp_router_id[50];
      sprintf(char_time_stamp, "%u", time_stamp_generated); 
      //zlog_debug("We are going to change to char the time stamp");
      sprintf(char_version_time_stamp_router_id, "%u", time_stamp_generated); 
      //zlog_debug("We did  change to char the time stamp");

      strcat(char_version_time_stamp_router_id, ","); //lets append router id to time stamp

      //zlog_debug("We added , to time stamp %s",char_version_time_stamp_router_id);
      uint32_t my_router_id = peer->local_as;

      char char_my_router_id[50];
      sprintf(char_my_router_id, "%u", my_router_id); 


      strcat(char_version_time_stamp_router_id, char_my_router_id);
     // zlog_debug("We concat router it to  time stamp %s",char_version_time_stamp_router_id);




      uint32_t generated_root_cause_id = 100 + rand() / (RAND_MAX / (1000 - 100 + 1) + 1);

      

      zlog_debug("Time Stamp and E_ID  and owner id is are %s and  %ld  and %ld ******** and time_stamp_rout_id %s for prefix %s and in_neighbor is %s",char_time_stamp,generated_root_cause_id,peer->local_as,char_version_time_stamp_router_id,inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ),peer->host);

    //   zlog_debug("******************the cause of %s with time stamp %s sent to %s is GRCMSG generated by  %ld ",inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ),char_milliseconds,peer->host,peer->as);

      char * in_received_time_stamp[50];
      strncpy(in_received_time_stamp, "GRCMSG", 50);
      //char *in_received_time_stamp[] = "GRCMSG";
      add_to_sent(&(a_peer_for_maintating_head_of_data_structure -> sent),char_time_stamp,peer, generated_root_cause_id, inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ));
      addcause(&(a_peer_for_maintating_head_of_data_structure -> cause),char_time_stamp,in_received_time_stamp,generated_root_cause_id, peer->local_as,inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ), "1 2 3", in_received_time_stamp,peer);

      //zlog_debug("******************");


      stream_putl (s, time_stamp_generated);
        /* 2: Write root cause event ID */
      stream_putl (s, generated_root_cause_id);

      root_cause_event_owner = peer->local_as;

      stream_putl (s, root_cause_event_owner);

    }

    

   

   

    /* 2: Write router ID */
   //stream_putl (s, peer->router-id);

    /* 2: withdrawn routes length */
    stream_putw (s, 0);


	  /* 3: total attributes length - attrlen_pos stores the position */
	  attrlen_pos = stream_get_endp (s);
	  stream_putw (s, 0);

	  /* 4: if there is MP_REACH_NLRI attribute, that should be the first
	   * attribute, according to draft-ietf-idr-error-handling. Save the
	   * position.
	   */
	  mpattr_pos = stream_get_endp(s);

	  /* 5: Encode all the attributes, except MP_REACH_NLRI attr. */
	  total_attr_len = bgp_packet_attribute (NULL, peer, s,
	                                         adv->baa->attr,
                                                 ((afi == AFI_IP && safi == SAFI_UNICAST) ?
                                                  &rn->p : NULL),
                                                 afi, safi,
	                                         from, prd, tag);
          space_remaining = STREAM_CONCAT_REMAIN (s, snlri, STREAM_SIZE(s)) -
                            BGP_MAX_PACKET_SIZE_OVERFLOW;
          space_needed = BGP_NLRI_LENGTH + bgp_packet_mpattr_prefix_size (afi, safi, &rn->p);;

          /* If the attributes alone do not leave any room for NLRI then
           * return */
          if (space_remaining < space_needed)
            {
              zlog_err ("%s cannot send UPDATE, the attributes do not leave "
                        "room for NLRI", peer->host);
              /* Flush the FIFO update queue */
              while (adv)
                adv = bgp_advertise_clean (peer, adv->adj, afi, safi);
              return NULL;
            } 

	}

      if (afi == AFI_IP && safi == SAFI_UNICAST)
      {

   

	      stream_put_prefix (s, &rn->p);
      }
      else
	{
	  /* Encode the prefix in MP_REACH_NLRI attribute */
	  struct prefix_rd *prd = NULL;
	  u_char *tag = NULL;

	  if (rn->prn)
	    prd = (struct prefix_rd *) &rn->prn->p;
	  if (binfo && binfo->extra)
	    tag = binfo->extra->tag;

	  if (stream_empty(snlri))
	    mpattrlen_pos = bgp_packet_mpattr_start(snlri, afi, safi,
						    adv->baa->attr);
	  bgp_packet_mpattr_prefix(snlri, afi, safi, &rn->p, prd, tag);
	}
      if (BGP_DEBUG (update, UPDATE_OUT))
        {
          char buf[INET6_BUFSIZ];

          zlog (peer->log, LOG_DEBUG, "%s send UPDATE %s/%d",
                peer->host,
                inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ),
                rn->p.prefixlen);
        }

      /* Synchnorize attribute.  */
      if (adj->attr)
	bgp_attr_unintern (&adj->attr);
      else
	peer->scount[afi][safi]++;

      adj->attr = bgp_attr_intern (adv->baa->attr);

      adv = bgp_advertise_clean (peer, adj, afi, safi);
    }


  zlog_debug ("%s We are after  the while (adv)", ".....---------..................");



  if (! stream_empty (s))
    {
      if (!stream_empty(snlri))
	{
	  bgp_packet_mpattr_end(snlri, mpattrlen_pos);
	  total_attr_len += stream_get_endp(snlri);
	}

      /* set the total attribute length correctly */
      stream_putw_at (s, attrlen_pos, total_attr_len);

      if (!stream_empty(snlri))
	packet = stream_dupcat(s, snlri, mpattr_pos);
      else
	packet = stream_dup (s);
      bgp_packet_set_size (packet);
      bgp_packet_add (peer, packet);
      BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
      stream_reset (s);
      stream_reset (snlri);
      zlog_debug ("%s We are at the returning packet point", ".....---------..................");
    
      return packet;
    }



  return NULL;
}

static struct stream *
bgp_update_packet_eor (struct peer *peer, afi_t afi, safi_t safi)
{
  struct stream *s;

  if (DISABLE_BGP_ANNOUNCE)
    return NULL;

  if (BGP_DEBUG (normal, NORMAL))
    zlog_debug ("send End-of-RIB for %s to %s", afi_safi_print (afi, safi), peer->host);

  s = stream_new (BGP_MAX_PACKET_SIZE);

  /* Make BGP update packet. */
  bgp_packet_set_marker (s, BGP_MSG_UPDATE);

  /* Unfeasible Routes Length */
  stream_putw (s, 0);

  if (afi == AFI_IP && safi == SAFI_UNICAST)
    {
      /* Total Path Attribute Length */
      stream_putw (s, 0);
    }
  else
    {
      /* Total Path Attribute Length */
      stream_putw (s, 6);
      stream_putc (s, BGP_ATTR_FLAG_OPTIONAL);
      stream_putc (s, BGP_ATTR_MP_UNREACH_NLRI);
      stream_putc (s, 3);
      stream_putw (s, afi);
      stream_putc (s, safi);
    }

  bgp_packet_set_size (s);
  bgp_packet_add (peer, s);
  return s;
}

/* Make BGP withdraw packet.  */
/* For ipv4 unicast:
   16-octet marker | 2-octet length | 1-octet type |
    2-octet withdrawn route length | withdrawn prefixes | 2-octet attrlen (=0)
*/
/* For other afi/safis:
   16-octet marker | 2-octet length | 1-octet type |
    2-octet withdrawn route length (=0) | 2-octet attrlen |
     mp_unreach attr type | attr len | afi | safi | withdrawn prefixes
*/
static struct stream *
bgp_withdraw_packet (struct peer *peer, afi_t afi, safi_t safi)
{


  zlog_debug ("we are going to build a withdraw packet for %s ",peer->host);

  struct stream *s;
  struct stream *packet;
  struct bgp_adj_out *adj;
  struct bgp_advertise *adv;
  struct bgp_node *rn;
  bgp_size_t unfeasible_len;
  bgp_size_t total_attr_len;
  size_t mp_start = 0;
  size_t attrlen_pos = 0;
  size_t mplen_pos = 0;
  u_char first_time = 1;
  int space_remaining = 0;
  int space_needed = 0;

  s = peer->work;
  stream_reset (s);

  while ((adv = BGP_ADV_FIFO_HEAD (&peer->sync[afi][safi]->withdraw)) != NULL)
    {
      assert (adv->rn);
      adj = adv->adj;
      rn = adv->rn;

      space_remaining = STREAM_REMAIN (s) -
                        BGP_MAX_PACKET_SIZE_OVERFLOW;
      space_needed = (BGP_NLRI_LENGTH + BGP_TOTAL_ATTR_LEN +
                      bgp_packet_mpattr_prefix_size (afi, safi, &rn->p));

      if (space_remaining < space_needed)
	break;

      if (stream_empty (s))
	{
	  bgp_packet_set_marker (s, BGP_MSG_UPDATE);
	  //stream_putw (s, 0); /* unfeasible routes length */
    stream_putl (s, 123456); /* root cause id */
    stream_putl (s, 123456); /* local time stamp */
    stream_putl (s, 1234); /* router id */

	}
      else
	first_time = 0;



      if (afi == AFI_IP && safi == SAFI_UNICAST)
	stream_put_prefix (s, &rn->p);
      else
	{
	  struct prefix_rd *prd = NULL;

	  if (rn->prn)
	    prd = (struct prefix_rd *) &rn->prn->p;

	  /* If first time, format the MP_UNREACH header */
	  if (first_time)
	    {
	      attrlen_pos = stream_get_endp (s);
	      /* total attr length = 0 for now. reevaluate later */
	      stream_putw (s, 0);
	      mp_start = stream_get_endp (s);
	      mplen_pos = bgp_packet_mpunreach_start(s, afi, safi);
	    }

	  bgp_packet_mpunreach_prefix(s, &rn->p, afi, safi, prd, NULL);
	}

      if (BGP_DEBUG (update, UPDATE_OUT))
        {
          char buf[INET6_BUFSIZ];

          zlog (peer->log, LOG_DEBUG, "%s send UPDATE %s/%d -- unreachable",
                peer->host,
                inet_ntop (rn->p.family, &(rn->p.u.prefix), buf, INET6_BUFSIZ),
                rn->p.prefixlen);
        }

      peer->scount[afi][safi]--;

      bgp_adj_out_remove (rn, adj, peer, afi, safi);
      bgp_unlock_node (rn);
    }

  if (! stream_empty (s))
    {
      if (afi == AFI_IP && safi == SAFI_UNICAST)
	{
	  unfeasible_len
	    = stream_get_endp (s) - BGP_HEADER_SIZE - BGP_UNFEASIBLE_LEN;
	  stream_putw_at (s, BGP_HEADER_SIZE, unfeasible_len);
	  stream_putw (s, 0);
	}
      else
	{
	  /* Set the mp_unreach attr's length */
	  bgp_packet_mpunreach_end(s, mplen_pos);

	  /* Set total path attribute length. */
	  total_attr_len = stream_get_endp(s) - mp_start;
	  stream_putw_at (s, attrlen_pos, total_attr_len);
	}
      bgp_packet_set_size (s);
      packet = stream_dup (s);
      bgp_packet_add (peer, packet);
      stream_reset (s);

      zlog_debug ("we are returning the withdraw packet for %s ",peer->host);

      return packet;
    }

  return NULL;
}

void
bgp_default_update_send (struct peer *peer, struct attr *attr,
			 afi_t afi, safi_t safi, struct peer *from)
{



  //zlog_debug ("we are at  bgp_default_update_send");



  struct stream *s;
  struct prefix p;
  unsigned long pos;
  bgp_size_t total_attr_len;

  if (DISABLE_BGP_ANNOUNCE)
    return;

  if (afi == AFI_IP)
    str2prefix ("0.0.0.0/0", &p);
  else 
    str2prefix ("::/0", &p);

  /* Logging the attribute. */
  if (BGP_DEBUG (update, UPDATE_OUT))
    {
      char attrstr[BUFSIZ];
      char buf[INET6_BUFSIZ];
      attrstr[0] = '\0';

      bgp_dump_attr (peer, attr, attrstr, BUFSIZ);
      zlog (peer->log, LOG_DEBUG, "%s send UPDATE %s/%d %s",
	    peer->host, inet_ntop(p.family, &(p.u.prefix), buf, INET6_BUFSIZ),
	    p.prefixlen, attrstr);
    }

  s = stream_new (BGP_MAX_PACKET_SIZE);

  /* Make BGP update packet. */
  bgp_packet_set_marker (s, BGP_MSG_UPDATE);

  /* Unfeasible Routes Length. */
  stream_putw (s, 0);

  /* Make place for total attribute length.  */
  pos = stream_get_endp (s);
  stream_putw (s, 0);
  total_attr_len = bgp_packet_attribute (NULL, peer, s, attr, &p, afi, safi, from, NULL, NULL);

  /* Set Total Path Attribute Length. */
  stream_putw_at (s, pos, total_attr_len);

  /* NLRI set. */
  if (p.family == AF_INET && safi == SAFI_UNICAST)
    stream_put_prefix (s, &p);

  /* Set size. */
  bgp_packet_set_size (s);

  /* Dump packet if debug option is set. */
#ifdef DEBUG
  /* bgp_packet_dump (packet); */
#endif /* DEBUG */

  /* Add packet to the peer. */
  bgp_packet_add (peer, s);

  BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
}

void
bgp_default_withdraw_send (struct peer *peer, afi_t afi, safi_t safi)
{
  struct stream *s;
  struct prefix p;
  unsigned long attrlen_pos = 0;
  unsigned long cp;
  bgp_size_t unfeasible_len;
  bgp_size_t total_attr_len;
  size_t mp_start = 0;
  size_t mplen_pos = 0;

  if (DISABLE_BGP_ANNOUNCE)
    return;

  if (afi == AFI_IP)
    str2prefix ("0.0.0.0/0", &p);
  else 
    str2prefix ("::/0", &p);

  total_attr_len = 0;

  if (BGP_DEBUG (update, UPDATE_OUT))
    {
      char buf[INET6_BUFSIZ];

      zlog (peer->log, LOG_DEBUG, "%s send UPDATE %s/%d -- unreachable",
            peer->host, inet_ntop(p.family, &(p.u.prefix), buf, INET6_BUFSIZ),
            p.prefixlen);
    }

  s = stream_new (BGP_MAX_PACKET_SIZE);

  /* Make BGP update packet. */
  bgp_packet_set_marker (s, BGP_MSG_UPDATE);

  /* Unfeasible Routes Length. */;
  cp = stream_get_endp (s);
  stream_putw (s, 0);

  /* Withdrawn Routes. */
  if (p.family == AF_INET && safi == SAFI_UNICAST)
    {
      stream_put_prefix (s, &p);

      unfeasible_len = stream_get_endp (s) - cp - 2;

      /* Set unfeasible len.  */
      stream_putw_at (s, cp, unfeasible_len);

      /* Set total path attribute length. */
      stream_putw (s, 0);
    }
  else
    {
      attrlen_pos = stream_get_endp (s);
      stream_putw (s, 0);
      mp_start = stream_get_endp (s);
      mplen_pos = bgp_packet_mpunreach_start(s, afi, safi);
      bgp_packet_mpunreach_prefix(s, &p, afi, safi, NULL, NULL);

      /* Set the mp_unreach attr's length */
      bgp_packet_mpunreach_end(s, mplen_pos);

      /* Set total path attribute length. */
      total_attr_len = stream_get_endp(s) - mp_start;
      stream_putw_at (s, attrlen_pos, total_attr_len);
    }

  bgp_packet_set_size (s);

  /* Add packet to the peer. */
  bgp_packet_add (peer, s);

  BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
}

/* Get next packet to be written.  */
static struct stream *
bgp_write_packet (struct peer *peer)
{
  afi_t afi;
  safi_t safi;
  struct stream *s = NULL;
  struct bgp_advertise *adv;

  s = stream_fifo_head (peer->obuf);
  if (s)
    return s;

  for (afi = AFI_IP; afi < AFI_MAX; afi++)
    for (safi = SAFI_UNICAST; safi < SAFI_MAX; safi++)
      {
	adv = BGP_ADV_FIFO_HEAD (&peer->sync[afi][safi]->withdraw);
	if (adv)
	  {
	    s = bgp_withdraw_packet (peer, afi, safi);
	    if (s)
	      return s;
	  }
      }
    
  for (afi = AFI_IP; afi < AFI_MAX; afi++)
    for (safi = SAFI_UNICAST; safi < SAFI_MAX; safi++)
      {
	adv = BGP_ADV_FIFO_HEAD (&peer->sync[afi][safi]->update);
	if (adv)
	  {
            if (adv->binfo && adv->binfo->uptime < peer->synctime)
	      {
		if (CHECK_FLAG (adv->binfo->peer->cap, PEER_CAP_RESTART_RCV)
		    && CHECK_FLAG (adv->binfo->peer->cap, PEER_CAP_RESTART_ADV)
		    && ! (CHECK_FLAG (adv->binfo->peer->cap,
                                      PEER_CAP_RESTART_BIT_RCV) &&
		          CHECK_FLAG (adv->binfo->peer->cap,
                                      PEER_CAP_RESTART_BIT_ADV))
		    && ! CHECK_FLAG (adv->binfo->flags, BGP_INFO_STALE)
		    && safi != SAFI_MPLS_VPN)
		  {
		    if (CHECK_FLAG (adv->binfo->peer->af_sflags[afi][safi],
			PEER_STATUS_EOR_RECEIVED))
		      s = bgp_update_packet (peer, afi, safi);
		  }
		else
		  s = bgp_update_packet (peer, afi, safi);
	      }

	    if (s)
	      return s;
	  }

	if (CHECK_FLAG (peer->cap, PEER_CAP_RESTART_RCV))
	  {
	    if (peer->afc_nego[afi][safi] && peer->synctime
		&& ! CHECK_FLAG (peer->af_sflags[afi][safi], PEER_STATUS_EOR_SEND)
		&& safi != SAFI_MPLS_VPN)
	      {
		SET_FLAG (peer->af_sflags[afi][safi], PEER_STATUS_EOR_SEND);
		return bgp_update_packet_eor (peer, afi, safi);
	      }
	  }
      }

  return NULL;
}

/* Is there partially written packet or updates we can send right
   now.  */
static int
bgp_write_proceed (struct peer *peer)
{
  afi_t afi;
  safi_t safi;
  struct bgp_advertise *adv;

  if (stream_fifo_head (peer->obuf))
    return 1;

  for (afi = AFI_IP; afi < AFI_MAX; afi++)
    for (safi = SAFI_UNICAST; safi < SAFI_MAX; safi++)
      if (FIFO_HEAD (&peer->sync[afi][safi]->withdraw))
	return 1;

  for (afi = AFI_IP; afi < AFI_MAX; afi++)
    for (safi = SAFI_UNICAST; safi < SAFI_MAX; safi++)
      if ((adv = BGP_ADV_FIFO_HEAD (&peer->sync[afi][safi]->update)) != NULL)
	if (adv->binfo->uptime < peer->synctime)
	  return 1;

  return 0;
}

/* Write packet to the peer. */
int
bgp_write (struct thread *thread)
{
  struct peer *peer;
  u_char type;
  struct stream *s; 
  int num;
  unsigned int count = 0;

  /* Yes first of all get peer pointer. */
  peer = THREAD_ARG (thread);
  peer->t_write = NULL;

  /* For non-blocking IO check. */
  if (peer->status == Connect)
    {
      bgp_connect_check (peer);
      return 0;
    }

  s = bgp_write_packet (peer);
  if (!s)
    return 0;	/* nothing to send */

  sockopt_cork (peer->fd, 1);

  /* Nonblocking write until TCP output buffer is full.  */
  do
    {
      int writenum;

      /* Number of bytes to be sent.  */
      writenum = stream_get_endp (s) - stream_get_getp (s);

      /* Call write() system call.  */
      num = write (peer->fd, STREAM_PNT (s), writenum);
      if (num < 0)
	{
	  /* write failed either retry needed or error */
	  if (ERRNO_IO_RETRY(errno))
		break;

          BGP_EVENT_ADD (peer, TCP_fatal_error);
	  return 0;
	}

      if (num != writenum)
	{
	  /* Partial write */
	  stream_forward_getp (s, num);
	  break;
	}

      /* Retrieve BGP packet type. */
      stream_set_getp (s, BGP_MARKER_SIZE + 2);
      type = stream_getc (s);

      switch (type)
	{
	case BGP_MSG_OPEN:
	  peer->open_out++;
	  break;
	case BGP_MSG_UPDATE:
	  peer->update_out++;
	  break;
    case BGP_MSG_CONVERGENCE:
      //zlog_debug ("The type of sending packet is BGP_MSG_CONVERGENCE");
      //bgp_fizzle_send (peer); // We have called this in another place!
      break;
  case BGP_MSG_FIZZLE:
      //zlog_debug ("The type of sending packet is FIZZLE, this has been triggered by bgp_fizzle_send");
      //bgp_fizzle_send (peer); // We have called this in another place!
      break;
	case BGP_MSG_NOTIFY:
	  peer->notify_out++;

	  /* Flush any existing events */
	  BGP_EVENT_ADD (peer, BGP_Stop_with_error);
	  goto done;

	case BGP_MSG_KEEPALIVE:
	  peer->keepalive_out++;
	  break;
	case BGP_MSG_ROUTE_REFRESH_NEW:
	case BGP_MSG_ROUTE_REFRESH_OLD:
	  peer->refresh_out++;
	  break;
	case BGP_MSG_CAPABILITY:
	  peer->dynamic_cap_out++;
	  break;
	}

      /* OK we send packet so delete it. */
      bgp_packet_delete (peer);
    }
  while (++count < BGP_WRITE_PACKET_MAX &&
	 (s = bgp_write_packet (peer)) != NULL);
  
  if (bgp_write_proceed (peer))
    BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);

 done:
  sockopt_cork (peer->fd, 0);
  return 0;
}

/* This is only for sending NOTIFICATION message to neighbor. */
static int
bgp_write_notify (struct peer *peer)
{

  zlog_debug (" We are in bgp_write_notify");


  int ret, val;
  u_char type;
  struct stream *s; 

  /* There should be at least one packet. */
  s = stream_fifo_head (peer->obuf);
  if (!s)
    return 0;
  assert (stream_get_endp (s) >= BGP_HEADER_SIZE);

  /* Stop collecting data within the socket */
  sockopt_cork (peer->fd, 0);

  /* socket is in nonblocking mode, if we can't deliver the NOTIFY, well,
   * we only care about getting a clean shutdown at this point. */
  ret = write (peer->fd, STREAM_DATA (s), stream_get_endp (s));

  /* only connection reset/close gets counted as TCP_fatal_error, failure
   * to write the entire NOTIFY doesn't get different FSM treatment */
  if (ret <= 0)
    {
      BGP_EVENT_ADD (peer, TCP_fatal_error);
      return 0;
    }

  /* Disable Nagle, make NOTIFY packet go out right away */
  val = 1;
  (void) setsockopt (peer->fd, IPPROTO_TCP, TCP_NODELAY,
                            (char *) &val, sizeof (val));

  /* Retrieve BGP packet type. */
  stream_set_getp (s, BGP_MARKER_SIZE + 2);
  type = stream_getc (s);

  assert (type == BGP_MSG_NOTIFY);

  /* Type should be notify. */
  peer->notify_out++;

  BGP_EVENT_ADD (peer, BGP_Stop_with_error);

  return 0;
}


/* Parse BGP CONVERGENCE packet */
static int
bgp_convergence_receive (struct peer *peer, bgp_size_t size)
{


  int ret, nlri_ret;
  u_char *end;
  struct stream *s;
  struct attr attr;
  struct attr_extra extra;
  bgp_size_t attribute_len;
  bgp_size_t update_len;
  bgp_size_t withdraw_len;
  int i;
  
  enum NLRI_TYPES {
    NLRI_UPDATE,
    NLRI_WITHDRAW,
    NLRI_MP_UPDATE,
    NLRI_MP_WITHDRAW,
    NLRI_TYPE_MAX,
  };
  struct bgp_nlri nlris[NLRI_TYPE_MAX];

  /* Status must be Established. */
  if (peer->status != Established) 
    {
      zlog_err ("%s [FSM] Update packet received under status %s",
    peer->host, LOOKUP (bgp_status_msg, peer->status));
      bgp_notify_send (peer, BGP_NOTIFY_FSM_ERR, 0);
      return -1;
    }

  /* Set initial values. */
  memset (&attr, 0, sizeof (struct attr));
  memset (&extra, 0, sizeof (struct attr_extra));
  memset (&nlris, 0, sizeof nlris);

  attr.extra = &extra;

  s = peer->ibuf;

      char result7[50]; 
   
    sprintf(result7, "%u", stream_pnt (s)); 

    //zlog_debug ("%s 1. this is the stream_pnt (s) ", result7);
      char result8[50]; 
    sprintf(result8, "%u", size); 
    //zlog_debug ("%s 2. this is the size ", result8);
    end = stream_pnt (s) + size;
      char result9[50]; 
   
    sprintf(result9, "%u", end); 

    //zlog_debug ("%s 3.this is the end  ", result9);

  if (  size == 4 )
    {
    //zlog_debug ("we have received a convergence message but the lenght is 4 ");

      return -1;
    }

  /* RFC1771 6.3 If the Unfeasible Routes Length or Total Attribute
     Length is too large (i.e., if Unfeasible Routes Length + Total
     Attribute Length + 23 exceeds the message Length), then the Error
     Subcode is set to Malformed Attribute List.  */
  if (stream_pnt (s) + 2 > end)
    {
      zlog_debug ("For convergence message:[Error] Update packet error (packet length is short");
      zlog_err ("%s [Error] Update packet error"
    " (packet length is short for unfeasible length)",
    peer->host);
      bgp_notify_send (peer, BGP_NOTIFY_UPDATE_ERR, 
           BGP_NOTIFY_UPDATE_MAL_ATTR);
      return -1;
    }

  //zlog_debug ("we have received a convergence message and we are going to get root cause event id and send to the received E's neighbors");

    int size_of_stream = s->size;

    char char_size_of_stream[50]; 
   
    sprintf(char_size_of_stream, "%u", size_of_stream); 

    //zlog_debug ("%s this is size_of_stream", char_size_of_stream);

    int end_of_s = s->endp;

    char char_end_of_s[50]; 
   
    sprintf(char_end_of_s, "%u", end_of_s); 

    //zlog_debug ("%s this is end_of_s", char_end_of_s);


  /* get root cause evnt id. */

  uint32_t rec_prefix_part_0;
  uint32_t rec_prefix_part_1;
  uint32_t rec_prefix_part_2;
  uint32_t rec_prefix_part_3;
  uint32_t rec_root_cause_event_id;

  rec_root_cause_event_id = stream_getl (s);

  zlog_debug ("We received a convergene message from %s ", peer->host);

  zlog_debug ("................................********  STABLE STATE of %ld ********............................",rec_root_cause_event_id);


  rec_prefix_part_0 = stream_getl (s);
  rec_prefix_part_1 = stream_getl (s);
  rec_prefix_part_2 = stream_getl (s);
  rec_prefix_part_3 = stream_getl (s);


  char received_prefix[50];
  sprintf( received_prefix, "%u", rec_prefix_part_0 );


  char char_received_prefix2[50];
  sprintf( char_received_prefix2, "%u", rec_prefix_part_1 );


  char char_received_prefix3[50];
  sprintf( char_received_prefix3, "%u", rec_prefix_part_2 );

    char char_received_prefix4[50];
  sprintf( char_received_prefix4, "%u", rec_prefix_part_3 );

  strcat(received_prefix, ".");
  strcat(received_prefix, char_received_prefix2);
  strcat(received_prefix, ".");
  strcat(received_prefix, char_received_prefix3);
  strcat(received_prefix, ".");
  strcat(received_prefix, char_received_prefix4);

  //zlog_debug ("This is %s the received prefix from bgp_convergence_receive function ", received_prefix);


  struct neighbours_of_a_prefix * test_neighbour_of_prefix = (struct neighbours_of_a_prefix *) malloc (sizeof(struct neighbours_of_a_prefix));

  test_neighbour_of_prefix = get_from_neighbours_of_a_prefix(&(a_peer_for_maintating_head_of_data_structure -> neighbours_of_prefix),received_prefix);
    
    if (test_neighbour_of_prefix != NULL)
    {
        //zlog_debug (" this is  our peer we have sent an update to");

        struct peer_list * my_temp = test_neighbour_of_prefix -> peer_list ;
        
        //zlog_debug("********** going to print the peer list ********");
        while(my_temp != NULL)
        {
            
            //zlog_debug("this is the host of the peer %s", my_temp -> peer -> host);

            bgp_convergence_send (my_temp -> peer,rec_root_cause_event_id, received_prefix);
            
            my_temp = my_temp -> next;
         }
    }
    else
        zlog_debug (" We have not sent prefix %s to any neighbor!!!",received_prefix);


  return 1;

}





/* Parse BGP FIZZLE packet */
static int
bgp_fizzle_receive (struct peer *peer, bgp_size_t size)
{



  int ret, nlri_ret;
  u_char *end;
  struct stream *s;
  struct attr attr;
  struct attr_extra extra;
  bgp_size_t attribute_len;
  bgp_size_t update_len;
  bgp_size_t withdraw_len;
  int i;
  
  enum NLRI_TYPES {
    NLRI_UPDATE,
    NLRI_WITHDRAW,
    NLRI_MP_UPDATE,
    NLRI_MP_WITHDRAW,
    NLRI_TYPE_MAX,
  };
  struct bgp_nlri nlris[NLRI_TYPE_MAX];

  /* Status must be Established. */
  if (peer->status != Established) 
    {
      zlog_err ("%s [FSM] Update packet received under status %s",
    peer->host, LOOKUP (bgp_status_msg, peer->status));
      bgp_notify_send (peer, BGP_NOTIFY_FSM_ERR, 0);
      return -1;
    }

  /* Set initial values. */
  memset (&attr, 0, sizeof (struct attr));
  memset (&extra, 0, sizeof (struct attr_extra));
  memset (&nlris, 0, sizeof nlris);

  attr.extra = &extra;

  s = peer->ibuf;
  char result7[50]; 
   
  sprintf(result7, "%u", stream_pnt (s)); 

    //zlog_debug ("%s 1. this is the stream_pnt (s) ", result7);



      char result8[50]; 
   
    sprintf(result8, "%u", size); 

    //zlog_debug ("%s 2. this is the size ", result8);


    end = stream_pnt (s) + size;


      char result9[50]; 
   
    sprintf(result9, "%u", end); 

    //zlog_debug ("%s 3.this is the end  ", result9);




  if (  size == 4 )
    {
    zlog_debug ("we have received a fizzle message but the lenght is 4 ");

      return -1;
    }


  /* RFC1771 6.3 If the Unfeasible Routes Length or Total Attribute
     Length is too large (i.e., if Unfeasible Routes Length + Total
     Attribute Length + 23 exceeds the message Length), then the Error
     Subcode is set to Malformed Attribute List.  */
  if (stream_pnt (s) + 2 > end)
    {
      //zlog_debug ("For FIZZLE message:[Error] Update packet error (packet length is short");


      zlog_err ("%s [Error] Update packet error"
    " (packet length is short for unfeasible length)",
    peer->host);
      bgp_notify_send (peer, BGP_NOTIFY_UPDATE_ERR, 
           BGP_NOTIFY_UPDATE_MAL_ATTR);
      return -1;
    }





  //zlog_debug ("we have received a fizzle message and we are going to get root cause event id and time");

    int size_of_stream = s->size;

    char char_size_of_stream[50]; 
   
    sprintf(char_size_of_stream, "%u", size_of_stream); 

    //zlog_debug ("%s this is size_of_stream", char_size_of_stream);


    int end_of_s = s->endp;

    char char_end_of_s[50]; 
   
    sprintf(char_end_of_s, "%u", end_of_s); 

    //zlog_debug ("%s this is end_of_s", char_end_of_s);


  /* get root cause evnt id. */

  uint32_t rec_time_stamp_from_fizzle;
  uint32_t rec_root_cause_event_id_from_fizzle;
  uint32_t rec_root_cause_event_owener_id;
  uint32_t  received_prefix1;
  uint32_t  received_prefix2;
  uint32_t  received_prefix3;
  uint32_t  received_prefix4;
  rec_time_stamp_from_fizzle = stream_getl (s);

    //zlog_debug ("we got time stamp time");
  char char_receiveded_time_stamp[50];
  sprintf( char_receiveded_time_stamp, "%u", rec_time_stamp_from_fizzle );
  



    char result18[50]; 
   
    sprintf(result18, "%u", stream_pnt (s)); 

    //zlog_debug ("%s and after reading time stamp, this is the stream_pnt (s) ", result18);

    //zlog_debug ("we are going to get root cause id");

  rec_root_cause_event_id_from_fizzle = stream_getl (s);

  rec_root_cause_event_owener_id = stream_getl (s);

  received_prefix1 = stream_getl (s);
  received_prefix2 = stream_getl (s);
  received_prefix3 = stream_getl (s);
  received_prefix4 = stream_getl (s);

  char char_received_root_cause_event_id[50];
  sprintf( char_received_root_cause_event_id, "%u", rec_root_cause_event_id_from_fizzle );


  char char_owner_id[50];
  sprintf( char_owner_id, "%u", rec_root_cause_event_owener_id );


  char received_prefix[50];
  sprintf( received_prefix, "%u", received_prefix1 );


  char char_received_prefix2[50];
  sprintf( char_received_prefix2, "%u", received_prefix2 );


  char char_received_prefix3[50];
  sprintf( char_received_prefix3, "%u", received_prefix3 );


    char char_received_prefix4[50];
  sprintf( char_received_prefix4, "%u", received_prefix4 );

  strcat(received_prefix, ".");
  strcat(received_prefix, char_received_prefix2);
  strcat(received_prefix, ".");
  strcat(received_prefix, char_received_prefix3);
  strcat(received_prefix, ".");
  strcat(received_prefix, char_received_prefix4);

  zlog_debug ("We received a fizzle message from %s for prefix %s , time_stamp %s and E_id %ld ", peer->host,received_prefix,char_receiveded_time_stamp,char_received_root_cause_event_id);

  //zlog_debug ("%s 4.this is the received_prefix", received_prefix);


  // zlog_debug ("%s 1.this is the char_receiveded_time_stamp", char_receiveded_time_stamp);
  // zlog_debug ("%s 2.this is the received_root_cause_event_id", char_received_root_cause_event_id);
  // zlog_debug ("%s 3.this is the received_root_cause_event_owenr", char_owner_id);
  // zlog_debug ("%s 4.this is the received_prefix", received_prefix);


  // zlog_debug ("**** ... deleting from sent...****...We are going to delete %s for prefix %s from sent data list", char_receiveded_time_stamp,received_prefix);
  
  //print_sent(&(a_peer_for_maintating_head_of_data_structure -> sent));
  

    // if (check_if_sent_is_empty(&(a_peer_for_maintating_head_of_data_structure -> received_prefix), 8888, received_prefix))
    //   zlog_debug ("sent is empty before adding ");
    // else
    //   zlog_debug ("sent is not empty before adding ");

    // add_to_sent(&(a_peer_for_maintating_head_of_data_structure -> sent),"123", peer, 8888, received_prefix);


    // if (check_if_sent_is_empty(&(a_peer_for_maintating_head_of_data_structure -> received_prefix), 8888, received_prefix))
    //   zlog_debug ("sent is not empty after adding ");
    // else
    //   zlog_debug ("sent is empty after adding ");
    // delete_from_sent(&(a_peer_for_maintating_head_of_data_structure -> sent),"123", peer, 8888, received_prefix);

    // if (check_if_sent_is_empty(&(a_peer_for_maintating_head_of_data_structure -> received_prefix), 8888, received_prefix))
    //   zlog_debug ("sent is empty after deleting");
    // else
    //   zlog_debug ("sent is not empty after deleting");


  delete_from_sent(&(a_peer_for_maintating_head_of_data_structure -> sent),char_receiveded_time_stamp, peer,  rec_root_cause_event_id_from_fizzle, received_prefix );


    // if (check_if_sent_is_empty(&(a_peer_for_maintating_head_of_data_structure -> sent), rec_root_cause_event_owener_id, received_prefix))
    //   zlog_debug ("sent is empty after deleting");
    // else
    //   zlog_debug ("sent is not empty after deleting");

  if (check_if_sent_is_empty(&(a_peer_for_maintating_head_of_data_structure -> sent),rec_root_cause_event_id_from_fizzle , received_prefix))
    {
      if (rec_root_cause_event_owener_id ==  peer->local_as)
      {
            zlog_debug ("................................********  STABLE STATE DETECTION of %s ********............................",char_received_root_cause_event_id);
            //zlog_debug ("**************************** it seems my sent is empty and I am the owner of event it, the owner_id is %s and my id is %ld    ***************",char_owner_id,peer->as);
            //zlog_debug ("**************************** sending global convergence message for    %  ***************",char_received_root_cause_event_id);
            // for all prefix_neighbors:
            //   send_convergence_message()
      struct neighbours_of_a_prefix * test_neighbour_of_prefix = (struct neighbours_of_a_prefix *) malloc (sizeof(struct neighbours_of_a_prefix));

      test_neighbour_of_prefix = get_from_neighbours_of_a_prefix(&(a_peer_for_maintating_head_of_data_structure -> neighbours_of_prefix),received_prefix);
        
        if (test_neighbour_of_prefix != NULL)
        {
            //zlog_debug (" this is  our peer we have sent an update to");

            struct peer_list * my_temp = test_neighbour_of_prefix -> peer_list ;
            
            //zlog_debug("********** going to print the peer list ********");
            while(my_temp != NULL)
            {
                
               // zlog_debug("this is the host of the peer %s", my_temp -> peer -> host);

                bgp_convergence_send (my_temp -> peer,rec_root_cause_event_id_from_fizzle, received_prefix);
                
                my_temp = my_temp -> next;
             }
        }
        else
            zlog_debug (" We have not sent prefix %s to any neighbor!!!",received_prefix);
      }
      else
      {
        //zlog_debug ("**************************** it seems my sent is empty BUT I am not the owner of event it, the owner_id is %s and my id is %ld    ***************",char_owner_id,peer->as);

        struct cause* cause_of_fizzle = (struct cause*) malloc(sizeof(struct cause));
      
        cause_of_fizzle = getcause(&(a_peer_for_maintating_head_of_data_structure -> cause), char_receiveded_time_stamp);

    if (cause_of_fizzle != NULL)
      {
        //zlog_debug ("********** We are going to send back a fizzle to the cause of %s which is %s**********",char_receiveded_time_stamp,cause_of_fizzle->received_timestamp);
    
          //zlog_debug("We are going to send a FIZZLE message to %s for prefix %s with time stamp %s and event id %d and E owner is %ld ",cause_of_fizzle->neighbour->host,cause_of_fizzle -> prefix_str,cause_of_fizzle->received_timestamp,char_received_root_cause_event_id),rec_root_cause_event_owener_id;
          int my_converted_root_cause_id_second = (int)strtol(char_received_root_cause_event_id, (char **)NULL, 10); // strtol = STRing TO Long

          bgp_fizzle_send(cause_of_fizzle->neighbour,my_converted_root_cause_id_second,cause_of_fizzle->received_timestamp,cause_of_fizzle->router_id,cause_of_fizzle -> prefix_str,rec_root_cause_event_owener_id);
      }
      else
      {
        zlog_debug("Big Error!!!!! We did not get anything for received time stamp %s in case sent is empty after removing",char_receiveded_time_stamp);

      }


      }
    }
  else
    {
      //zlog_debug ("*************sent is not empty *************");

      print_sent(&(a_peer_for_maintating_head_of_data_structure -> sent));

      struct cause* temp_cause = (struct cause*) malloc(sizeof(struct cause));
      
      temp_cause = getcause(&(a_peer_for_maintating_head_of_data_structure -> cause), char_receiveded_time_stamp);
      //zlog_debug ("*************We got the cause of %s  *************",char_receiveded_time_stamp);

      if (temp_cause != NULL)
      {

        //zlog_debug("We are going to send a FIZZLE message to %s ",temp_cause->neighbour->host);

        //zlog_debug("We are going to send a FIZZLE message to %s for prefix %s ",temp_cause->neighbour->host,temp_cause -> prefix_str);


        //zlog_debug("We are going to send a FIZZLE message to %s for prefix %s with time stamp %s ",temp_cause->neighbour->host,temp_cause -> prefix_str,temp_cause->received_timestamp);


          zlog_debug("We are going to send a FIZZLE message to %s for prefix %s with time stamp %s and event id %d and E owner is %ld ",temp_cause->neighbour->host,temp_cause -> prefix_str,temp_cause->received_timestamp,char_received_root_cause_event_id),rec_root_cause_event_owener_id;
          int my_converted_root_cause_id = (int)strtol(char_received_root_cause_event_id, (char **)NULL, 10); // strtol = STRing TO Long

          bgp_fizzle_send(temp_cause->neighbour,my_converted_root_cause_id,temp_cause->received_timestamp,temp_cause->router_id,temp_cause -> prefix_str,rec_root_cause_event_owener_id);
      }
      else
      {
        zlog_debug("Error!!!!! We did not get anything for received time stamp %s in case sent is not empty",char_receiveded_time_stamp);

      }

    }



    

  return 1;

}









/* Make keepalive packet and send it to the peer. */
void
bgp_keepalive_send (struct peer *peer)
{
  //zlog_debug ("%s We are going to send our keep alive message", peer->host);

  struct stream *s;
  int length;

  s = stream_new (BGP_MAX_PACKET_SIZE);

  /* Make keepalive packet. */

  // we changed this to set our FIZZLE message type
  //bgp_packet_set_marker (s, BGP_MSG_KEEPALIVE);
  bgp_packet_set_marker (s, BGP_MSG_KEEPALIVE);

  /* Set packet size. */
  length = bgp_packet_set_size (s);

  /* Dump packet if debug option is set. */
  /* bgp_packet_dump (s); */
 
  if (BGP_DEBUG (keepalive, KEEPALIVE))  
    zlog_debug ("%s sending KEEPALIVE", peer->host); 
  if (BGP_DEBUG (normal, NORMAL))
    zlog_debug ("%s send message type %d, length (incl. header) %d",
               peer->host, BGP_MSG_KEEPALIVE, length);

  /* Add packet to the peer. */
  bgp_packet_add (peer, s);

  BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
}

/* Make open packet and send it to the peer. */
void
bgp_open_send (struct peer *peer)
{


  //zlog_debug ("we are at bgp_open_send");

  struct stream *s;
  int length;
  u_int16_t send_holdtime;
  as_t local_as;

  if (CHECK_FLAG (peer->config, PEER_CONFIG_TIMER))
    send_holdtime = peer->holdtime;
  else
    send_holdtime = peer->bgp->default_holdtime;

  /* local-as Change */
  if (peer->change_local_as)
    local_as = peer->change_local_as; 
  else
    local_as = peer->local_as; 

  s = stream_new (BGP_MAX_PACKET_SIZE);

  /* Make open packet. */
  bgp_packet_set_marker (s, BGP_MSG_OPEN);

  /* Set open packet values. */
  stream_putc (s, BGP_VERSION_4);        /* BGP version */
  stream_putw (s, (local_as <= BGP_AS_MAX) ? (u_int16_t) local_as 
                                           : BGP_AS_TRANS);
  stream_putw (s, send_holdtime);     	 /* Hold Time */
  stream_put_in_addr (s, &peer->local_id); /* BGP Identifier */

  /* Set capability code. */
  bgp_open_capability (s, peer);

  /* Set BGP packet length. */
  length = bgp_packet_set_size (s);

  if (BGP_DEBUG (normal, NORMAL))
    zlog_debug ("%s sending OPEN, version %d, my as %u, holdtime %d, id %s", 
	       peer->host, BGP_VERSION_4, local_as,
	       send_holdtime, inet_ntoa (peer->local_id));

  if (BGP_DEBUG (normal, NORMAL))
    zlog_debug ("%s send message type %d, length (incl. header) %d",
	       peer->host, BGP_MSG_OPEN, length);

  /* Dump packet if debug option is set. */
  /* bgp_packet_dump (s); */

  /* Add packet to the peer. */
  bgp_packet_add (peer, s);

  BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
}

/* Send BGP notify packet with data potion. */
void
bgp_notify_send_with_data (struct peer *peer, u_char code, u_char sub_code,
			   u_char *data, size_t datalen)
{
  struct stream *s;
  int length;

  /* Allocate new stream. */
  s = stream_new (BGP_MAX_PACKET_SIZE);

  /* Make nitify packet. */
  bgp_packet_set_marker (s, BGP_MSG_NOTIFY);

  /* Set notify packet values. */
  stream_putc (s, code);        /* BGP notify code */
  stream_putc (s, sub_code);	/* BGP notify sub_code */

  /* If notify data is present. */
  if (data)
    stream_write (s, data, datalen);
  
  /* Set BGP packet length. */
  length = bgp_packet_set_size (s);
  
  /* Add packet to the peer. */
  stream_fifo_clean (peer->obuf);
  bgp_packet_add (peer, s);

  /* For debug */
  {
    struct bgp_notify bgp_notify;
    int first = 0;
    int i;
    char c[4];

    bgp_notify.code = code;
    bgp_notify.subcode = sub_code;
    bgp_notify.data = NULL;
    bgp_notify.length = length - BGP_MSG_NOTIFY_MIN_SIZE;
    
    if (bgp_notify.length)
      {
	bgp_notify.data = XMALLOC (MTYPE_TMP, bgp_notify.length * 3);
	for (i = 0; i < bgp_notify.length; i++)
	  if (first)
	    {
	      sprintf (c, " %02x", data[i]);
	      strcat (bgp_notify.data, c);
	    }
	  else
	    {
	      first = 1;
	      sprintf (c, "%02x", data[i]);
	      strcpy (bgp_notify.data, c);
	    }
      }
    bgp_notify_print (peer, &bgp_notify, "sending");

    if (bgp_notify.data)
      {
        XFREE (MTYPE_TMP, bgp_notify.data);
        bgp_notify.data = NULL;
        bgp_notify.length = 0;
      }
  }

  if (BGP_DEBUG (normal, NORMAL))
    zlog_debug ("%s send message type %d, length (incl. header) %d",
	       peer->host, BGP_MSG_NOTIFY, length);

  /* peer reset cause */
  if (sub_code != BGP_NOTIFY_CEASE_CONFIG_CHANGE)
    {
      if (sub_code == BGP_NOTIFY_CEASE_ADMIN_RESET)
      {
        peer->last_reset = PEER_DOWN_USER_RESET;
        zlog_info ("Notification sent to neighbor %s:%u: User reset",
                   peer->host, sockunion_get_port (&peer->su));
      }
      else if (sub_code == BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN)
      {
        peer->last_reset = PEER_DOWN_USER_SHUTDOWN;
        zlog_info ("Notification sent to neighbor %s:%u shutdown",
                    peer->host, sockunion_get_port (&peer->su));
      }
      else
      {
        peer->last_reset = PEER_DOWN_NOTIFY_SEND;
        zlog_info ("Notification sent to neighbor %s:%u: type %u/%u",
                   peer->host, sockunion_get_port (&peer->su),
                   code, sub_code);
      }
    }
  else
     zlog_info ("Notification sent to neighbor %s:%u: configuration change",
                peer->host, sockunion_get_port (&peer->su));

  /* Call immediately. */
  BGP_WRITE_OFF (peer->t_write);

  bgp_write_notify (peer);
}

/* Send BGP notify packet. */
void
bgp_notify_send (struct peer *peer, u_char code, u_char sub_code)
{
  bgp_notify_send_with_data (peer, code, sub_code, NULL, 0);
}

/* Send route refresh message to the peer. */
void
bgp_route_refresh_send (struct peer *peer, afi_t afi, safi_t safi,
			u_char orf_type, u_char when_to_refresh, int remove)
{



   // zlog_debug (" We are in bgp_route_refresh_send");



  struct stream *s;
  int length;
  struct bgp_filter *filter;
  int orf_refresh = 0;

  if (DISABLE_BGP_ANNOUNCE)
    return;

  filter = &peer->filter[afi][safi];

  /* Adjust safi code. */
  if (safi == SAFI_MPLS_VPN)
    safi = SAFI_MPLS_LABELED_VPN;
  
  s = stream_new (BGP_MAX_PACKET_SIZE);

  /* Make BGP update packet. */
  if (CHECK_FLAG (peer->cap, PEER_CAP_REFRESH_NEW_RCV))
    bgp_packet_set_marker (s, BGP_MSG_ROUTE_REFRESH_NEW);
  else
    bgp_packet_set_marker (s, BGP_MSG_ROUTE_REFRESH_OLD);

  /* Encode Route Refresh message. */
  stream_putw (s, afi);
  stream_putc (s, 0);
  stream_putc (s, safi);
 
  if (orf_type == ORF_TYPE_PREFIX
      || orf_type == ORF_TYPE_PREFIX_OLD)
    if (remove || filter->plist[FILTER_IN].plist)
      {
	u_int16_t orf_len;
	unsigned long orfp;

	orf_refresh = 1; 
	stream_putc (s, when_to_refresh);
	stream_putc (s, orf_type);
	orfp = stream_get_endp (s);
	stream_putw (s, 0);

	if (remove)
	  {
	    UNSET_FLAG (peer->af_sflags[afi][safi], PEER_STATUS_ORF_PREFIX_SEND);
	    stream_putc (s, ORF_COMMON_PART_REMOVE_ALL);
	    if (BGP_DEBUG (normal, NORMAL))
	      zlog_debug ("%s sending REFRESH_REQ to remove ORF(%d) (%s) for afi/safi: %d/%d", 
			 peer->host, orf_type,
			 (when_to_refresh == REFRESH_DEFER ? "defer" : "immediate"),
			 afi, safi);
	  }
	else
	  {
	    SET_FLAG (peer->af_sflags[afi][safi], PEER_STATUS_ORF_PREFIX_SEND);
	    prefix_bgp_orf_entry (s, filter->plist[FILTER_IN].plist,
				  ORF_COMMON_PART_ADD, ORF_COMMON_PART_PERMIT,
				  ORF_COMMON_PART_DENY);
	    if (BGP_DEBUG (normal, NORMAL))
	      zlog_debug ("%s sending REFRESH_REQ with pfxlist ORF(%d) (%s) for afi/safi: %d/%d", 
			 peer->host, orf_type,
			 (when_to_refresh == REFRESH_DEFER ? "defer" : "immediate"),
			 afi, safi);
	  }

	/* Total ORF Entry Len. */
	orf_len = stream_get_endp (s) - orfp - 2;
	stream_putw_at (s, orfp, orf_len);
      }

  /* Set packet size. */
  length = bgp_packet_set_size (s);

  if (BGP_DEBUG (normal, NORMAL))
    {
      if (! orf_refresh)
	zlog_debug ("%s sending REFRESH_REQ for afi/safi: %d/%d", 
		   peer->host, afi, safi);
      zlog_debug ("%s send message type %d, length (incl. header) %d",
		 peer->host, CHECK_FLAG (peer->cap, PEER_CAP_REFRESH_NEW_RCV) ?
		 BGP_MSG_ROUTE_REFRESH_NEW : BGP_MSG_ROUTE_REFRESH_OLD, length);
    }

  /* Add packet to the peer. */
  bgp_packet_add (peer, s);

  BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
}

/* Send capability message to the peer. */
void
bgp_capability_send (struct peer *peer, afi_t afi, safi_t safi,
		     int capability_code, int action)
{
  struct stream *s;
  int length;

  /* Adjust safi code. */
  if (safi == SAFI_MPLS_VPN)
    safi = SAFI_MPLS_LABELED_VPN;

  s = stream_new (BGP_MAX_PACKET_SIZE);

  /* Make BGP update packet. */
  bgp_packet_set_marker (s, BGP_MSG_CAPABILITY);

  /* Encode MP_EXT capability. */
  if (capability_code == CAPABILITY_CODE_MP)
    {
      stream_putc (s, action);
      stream_putc (s, CAPABILITY_CODE_MP);
      stream_putc (s, CAPABILITY_CODE_MP_LEN);
      stream_putw (s, afi);
      stream_putc (s, 0);
      stream_putc (s, safi);

      if (BGP_DEBUG (normal, NORMAL))
        zlog_debug ("%s sending CAPABILITY has %s MP_EXT CAP for afi/safi: %d/%d",
		   peer->host, action == CAPABILITY_ACTION_SET ?
		   "Advertising" : "Removing", afi, safi);
    }

  /* Set packet size. */
  length = bgp_packet_set_size (s);


  /* Add packet to the peer. */
  bgp_packet_add (peer, s);

  if (BGP_DEBUG (normal, NORMAL))
    zlog_debug ("%s send message type %d, length (incl. header) %d",
	       peer->host, BGP_MSG_CAPABILITY, length);

  BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
}

/* RFC1771 6.8 Connection collision detection. */
static int
bgp_collision_detect (struct peer *new, struct in_addr remote_id)
{
  struct peer *peer;
  struct listnode *node, *nnode;
  struct bgp *bgp;

  bgp = bgp_get_default ();
  if (! bgp)
    return 0;
  
  /* Upon receipt of an OPEN message, the local system must examine
     all of its connections that are in the OpenConfirm state.  A BGP
     speaker may also examine connections in an OpenSent state if it
     knows the BGP Identifier of the peer by means outside of the
     protocol.  If among these connections there is a connection to a
     remote BGP speaker whose BGP Identifier equals the one in the
     OPEN message, then the local system performs the following
     collision resolution procedure: */

  for (ALL_LIST_ELEMENTS (bgp->peer, node, nnode, peer))
    {
      if (peer == new)
        continue;
      if (!sockunion_same (&peer->su, &new->su))
        continue;
      
      /* Unless allowed via configuration, a connection collision with an
         existing BGP connection that is in the Established state causes
         closing of the newly created connection. */
      if (peer->status == Established)
        {
          /* GR may do things slightly differently to classic RFC .  Punt to
           * open_receive, see below 
           */
          if (CHECK_FLAG (peer->sflags, PEER_STATUS_NSF_MODE))
            continue;
          
          if (new->fd >= 0)
            {
              if (BGP_DEBUG (events, EVENTS))
                 zlog_debug ("%s:%u Existing Established peer, sending NOTIFY",
                             new->host, sockunion_get_port (&new->su));
              bgp_notify_send (new, BGP_NOTIFY_CEASE, 
                               BGP_NOTIFY_CEASE_COLLISION_RESOLUTION);
            }
          return -1;
        }
      
      /* Note: Quagga historically orders explicitly only on the processing
       * of the Opens, treating 'new' as the passive, inbound and connection
       * and 'peer' as the active outbound connection.
       */
       
      /* The local_id is always set, so we can match the given remote-ID
       * from the OPEN against both OpenConfirm and OpenSent peers.
       */
      if (peer->status == OpenConfirm || peer->status == OpenSent)
	{
	  struct peer *out = peer;
	  struct peer *in = new;
	  int ret_close_out = 1, ret_close_in = -1;
	  
	  if (!CHECK_FLAG (new->sflags, PEER_STATUS_ACCEPT_PEER))
	    {
	      out = new;
	      ret_close_out = -1;
	      in = peer;
	      ret_close_in = 1;
	    }
          
	  /* 1. The BGP Identifier of the local system is compared to
	     the BGP Identifier of the remote system (as specified in
	     the OPEN message). */

	  if (ntohl (peer->local_id.s_addr) < ntohl (remote_id.s_addr))
	    {
	      /* 2. If the value of the local BGP Identifier is less
		 than the remote one, the local system closes BGP
		 connection that already exists (the one that is
		 already in the OpenConfirm state), and accepts BGP
		 connection initiated by the remote system. */

	      if (out->fd >= 0)
	        {
	          if (BGP_DEBUG (events, EVENTS))
	             zlog_debug ("%s Collision resolution, remote ID higher,"
	                         " closing outbound", peer->host);
		  bgp_notify_send (out, BGP_NOTIFY_CEASE, 
		                   BGP_NOTIFY_CEASE_COLLISION_RESOLUTION);
                }
	      return ret_close_out;
	    }
	  else
	    {
	      /* 3. Otherwise, the local system closes newly created
		 BGP connection (the one associated with the newly
		 received OPEN message), and continues to use the
		 existing one (the one that is already in the
		 OpenConfirm state). */

	      if (in->fd >= 0)
	        {
	          if (BGP_DEBUG (events, EVENTS))
	             zlog_debug ("%s Collision resolution, local ID higher,"
	                         " closing inbound", peer->host);

                  bgp_notify_send (in, BGP_NOTIFY_CEASE, 
			           BGP_NOTIFY_CEASE_COLLISION_RESOLUTION);
                }
	      return ret_close_in;
	    }
	}
    }
  return 0;
}

static int
bgp_open_receive (struct peer *peer, bgp_size_t size)
{
  int ret;
  u_char version;
  u_char optlen;
  u_int16_t holdtime;
  u_int16_t send_holdtime;
  as_t remote_as;
  as_t as4 = 0;
  struct peer *realpeer;
  struct in_addr remote_id;
  int mp_capability;
  u_int8_t notify_data_remote_as[2];
  u_int8_t notify_data_remote_id[4];

  realpeer = NULL;
  
  /* Parse open packet. */
  version = stream_getc (peer->ibuf);
  memcpy (notify_data_remote_as, stream_pnt (peer->ibuf), 2);
  remote_as  = stream_getw (peer->ibuf);
  holdtime = stream_getw (peer->ibuf);
  memcpy (notify_data_remote_id, stream_pnt (peer->ibuf), 4);
  remote_id.s_addr = stream_get_ipv4 (peer->ibuf);

  /* Receive OPEN message log  */
  if (BGP_DEBUG (normal, NORMAL))
    zlog_debug ("%s rcv OPEN, version %d, remote-as (in open) %u,"
                " holdtime %d, id %s, %sbound connection",
	        peer->host, version, remote_as, holdtime,
	        inet_ntoa (remote_id),
	        CHECK_FLAG(peer->sflags, PEER_STATUS_ACCEPT_PEER)
	          ? "in" : "out");
  
  /* BEGIN to read the capability here, but dont do it yet */
  mp_capability = 0;
  optlen = stream_getc (peer->ibuf);
  
  if (optlen != 0)
    {
      /* We need the as4 capability value *right now* because
       * if it is there, we have not got the remote_as yet, and without
       * that we do not know which peer is connecting to us now.
       */ 
      as4 = peek_for_as4_capability (peer, optlen);
    }
  
  /* Just in case we have a silly peer who sends AS4 capability set to 0 */
  if (CHECK_FLAG (peer->cap, PEER_CAP_AS4_RCV) && !as4)
    {
      zlog_err ("%s bad OPEN, got AS4 capability, but AS4 set to 0",
                peer->host);
      bgp_notify_send (peer, BGP_NOTIFY_OPEN_ERR,
                       BGP_NOTIFY_OPEN_BAD_PEER_AS);
      return -1;
    }
  
  if (remote_as == BGP_AS_TRANS)
    {
	  /* Take the AS4 from the capability.  We must have received the
	   * capability now!  Otherwise we have a asn16 peer who uses
	   * BGP_AS_TRANS, for some unknown reason.
	   */
      if (as4 == BGP_AS_TRANS)
        {
          zlog_err ("%s [AS4] NEW speaker using AS_TRANS for AS4, not allowed",
                    peer->host);
          bgp_notify_send (peer, BGP_NOTIFY_OPEN_ERR,
                 BGP_NOTIFY_OPEN_BAD_PEER_AS);
          return -1;
        }
      
      if (!as4 && BGP_DEBUG (as4, AS4))
        zlog_debug ("%s [AS4] OPEN remote_as is AS_TRANS, but no AS4."
                    " Odd, but proceeding.", peer->host);
      else if (as4 < BGP_AS_MAX && BGP_DEBUG (as4, AS4))
        zlog_debug ("%s [AS4] OPEN remote_as is AS_TRANS, but AS4 (%u) fits "
                    "in 2-bytes, very odd peer.", peer->host, as4);
      if (as4)
        remote_as = as4;
    } 
  else 
    {
      /* We may have a partner with AS4 who has an asno < BGP_AS_MAX */
      /* If we have got the capability, peer->as4cap must match remote_as */
      if (CHECK_FLAG (peer->cap, PEER_CAP_AS4_RCV)
          && as4 != remote_as)
        {
	  /* raise error, log this, close session */
	  zlog_err ("%s bad OPEN, got AS4 capability, but remote_as %u"
	            " mismatch with 16bit 'myasn' %u in open",
	            peer->host, as4, remote_as);
	  bgp_notify_send (peer, BGP_NOTIFY_OPEN_ERR,
			   BGP_NOTIFY_OPEN_BAD_PEER_AS);
	  return -1;
	}
    }

  /* Lookup peer from Open packet. */
  if (CHECK_FLAG (peer->sflags, PEER_STATUS_ACCEPT_PEER))
    {
      int as = 0;

      realpeer = peer_lookup_with_open (&peer->su, remote_as, &remote_id, &as);

      if (! realpeer)
	{
	  /* Peer's source IP address is check in bgp_accept(), so this
	     must be AS number mismatch or remote-id configuration
	     mismatch. */
	  if (as)
	    {
	      if (BGP_DEBUG (normal, NORMAL))
		zlog_debug ("%s bad OPEN, wrong router identifier %s",
			    peer->host, inet_ntoa (remote_id));
	      bgp_notify_send_with_data (peer, BGP_NOTIFY_OPEN_ERR, 
					 BGP_NOTIFY_OPEN_BAD_BGP_IDENT,
					 notify_data_remote_id, 4);
	    }
	  else
	    {
	      if (BGP_DEBUG (normal, NORMAL))
		zlog_debug ("%s bad OPEN, remote AS is %u, expected %u",
			    peer->host, remote_as, peer->as);
	      bgp_notify_send_with_data (peer, BGP_NOTIFY_OPEN_ERR,
					 BGP_NOTIFY_OPEN_BAD_PEER_AS,
					 notify_data_remote_as, 2);
	    }
	  return -1;
	}
    }

  /* When collision is detected and this peer is closed.  Retrun
     immidiately. */
  ret = bgp_collision_detect (peer, remote_id);
  if (ret < 0)
    return ret;

  /* Bit hacky */
  if (CHECK_FLAG (peer->sflags, PEER_STATUS_ACCEPT_PEER))
    { 
      /* Connection FSM state is intertwined with our peer configuration
       * (the RFC encourages this a bit).  At _this_ point we have a
       * 'realpeer' which represents the configuration and any earlier FSM
       * (outbound, unless the remote side has opened two connections to
       * us), and a 'peer' which here represents an inbound connection that
       * has not yet been reconciled with a 'realpeer'.  
       * 
       * As 'peer' has just sent an OPEN that reconciliation must now
       * happen, as only the 'realpeer' can ever proceed to Established.
       *
       * bgp_collision_detect should have resolved any collisions with
       * realpeers that are in states OpenSent, OpenConfirm or Established,
       * and may have sent a notify on the 'realpeer' connection. 
       * bgp_accept will have rejected any connections where the 'realpeer'
       * is in Idle or >Established (though, that status may have changed
       * since).
       *
       * Need to finish off any reconciliation here, and ensure that
       * 'realpeer' is left holding any needed state from the appropriate
       * connection (fd, buffers, etc.), and any state from the other
       * connection is cleaned up.
       */

      /* Is realpeer in some globally-down state, that precludes any and all
       * connections (Idle, Clearing, Deleted, etc.)?
       */
      if (realpeer->status == Idle || realpeer->status > Established)
        {
          if (BGP_DEBUG (events, EVENTS))
            zlog_debug ("%s peer status is %s, closing the new connection",
                        realpeer->host, 
                        LOOKUP (bgp_status_msg, realpeer->status));
          return -1;
        }
      
      /* GR does things differently, and prefers any new connection attempts
       * over an Established one (why not just rely on KEEPALIVE and avoid
       * having to special case this?) */
      if (realpeer->status == Established
	    && CHECK_FLAG (realpeer->sflags, PEER_STATUS_NSF_MODE))
	{
	  realpeer->last_reset = PEER_DOWN_NSF_CLOSE_SESSION;
	  SET_FLAG (realpeer->sflags, PEER_STATUS_NSF_WAIT);
	}
      else if (ret == 0) 
 	{
 	  /* If we're here, RFC collision-detect did not reconcile the
 	   * connections, and the 'realpeer' is still available.  So
 	   * 'realpeer' must be 'Active' or 'Connect'.
 	   *
 	   * According to the RFC we should just let this connection (of the
 	   * accepted 'peer') continue on to Established if the other
 	   * onnection (the 'realpeer') is in a more larval state, and
 	   * reconcile them when OPEN is sent on the 'realpeer'.
 	   *
 	   * However, the accepted 'peer' must be reconciled with 'peer' at
 	   * this point, due to the implementation, if 'peer' is to be able
 	   * to proceed.  So it should be allowed to go to Established, as
 	   * long as the 'realpeer' was in Active or Connect state - which
 	   * /should/ be the case if we're here.
 	   *
 	   * So we should only need to sanity check that that is the case
 	   * here, and allow the code to get on with transferring the 'peer'
 	   * connection state over.
 	   */
          if (realpeer->status != Active && realpeer->status != Connect)
            {
              if (BGP_DEBUG (events, EVENTS))
                zlog_warn ("%s real peer status should be Active or Connect,"
                            " but is %s",
                            realpeer->host, 
                            LOOKUP (bgp_status_msg, realpeer->status));
	      bgp_notify_send (realpeer, BGP_NOTIFY_CEASE,
			       BGP_NOTIFY_CEASE_COLLISION_RESOLUTION);
            }
 	}

      if (BGP_DEBUG (events, EVENTS))
	zlog_debug ("%s:%u [Event] Transfer accept BGP peer to real (state %s)",
		   peer->host, sockunion_get_port (&peer->su), 
		   LOOKUP (bgp_status_msg, realpeer->status));

      bgp_stop (realpeer);
      
      /* Transfer file descriptor. */
      realpeer->fd = peer->fd;
      peer->fd = -1;

      /* Transfer input buffer. */
      stream_free (realpeer->ibuf);
      realpeer->ibuf = peer->ibuf;
      realpeer->packet_size = peer->packet_size;
      peer->ibuf = NULL;
      
      /* Transfer output buffer, there may be an OPEN queued to send */
      stream_fifo_free (realpeer->obuf);
      realpeer->obuf = peer->obuf;
      peer->obuf = NULL;
      
      bool open_deferred
        = CHECK_FLAG (peer->sflags, PEER_STATUS_OPEN_DEFERRED);
      
      /* Transfer status. */
      realpeer->status = peer->status;
      bgp_stop (peer);
      
      /* peer pointer change */
      peer = realpeer;
      
      if (peer->fd < 0)
	{
	  zlog_err ("bgp_open_receive peer's fd is negative value %d",
		    peer->fd);
	  return -1;
	}
      //zlog_debug ("we are before BGP_READ_ON");

      BGP_READ_ON (peer->t_read, bgp_read, peer->fd);

      if (stream_fifo_head (peer->obuf))
      {
        //zlog_debug ("we are after BGP_READ_ON and before BGP_WRITE_ON");

        BGP_WRITE_ON (peer->t_write, bgp_write, peer->fd);
        //zlog_debug ("we are after BGP_WRITE_ON");

      }
      
      /* hack: we may defer OPEN on accept peers, when there seems to be a
       * realpeer in progress, when an accept peer connection is opened. This
       * is to avoid interoperability issues, with test/conformance tools
       * particularly. See bgp_fsm.c::bgp_connect_success
       *
       * If OPEN was deferred there, then we must send it now.
       */
      if (open_deferred)
      {
      //zlog_debug ("we are going to send bgp_open_send");

        bgp_open_send (peer);
      }
    }

  /* remote router-id check. */
  if (remote_id.s_addr == 0
      || IPV4_CLASS_DE (ntohl (remote_id.s_addr))
      || ntohl (peer->local_id.s_addr) == ntohl (remote_id.s_addr))
    {
      if (BGP_DEBUG (normal, NORMAL))
	zlog_debug ("%s bad OPEN, wrong router identifier %s",
		   peer->host, inet_ntoa (remote_id));
      bgp_notify_send_with_data (peer, 
				 BGP_NOTIFY_OPEN_ERR, 
				 BGP_NOTIFY_OPEN_BAD_BGP_IDENT,
				 notify_data_remote_id, 4);
      return -1;
    }

  /* Set remote router-id */
  peer->remote_id = remote_id;

  /* Peer BGP version check. */
  if (version != BGP_VERSION_4)
    {
      u_int16_t maxver = htons(BGP_VERSION_4);
      /* XXX this reply may not be correct if version < 4  XXX */
      if (BGP_DEBUG (normal, NORMAL))
	zlog_debug ("%s bad protocol version, remote requested %d, local request %d",
		   peer->host, version, BGP_VERSION_4);
      /* Data must be in network byte order here */
      bgp_notify_send_with_data (peer, 
				 BGP_NOTIFY_OPEN_ERR, 
				 BGP_NOTIFY_OPEN_UNSUP_VERSION,
				 (u_int8_t *) &maxver, 2);
      return -1;
    }

  /* Check neighbor as number. */
  if (remote_as != peer->as)
    {
      if (BGP_DEBUG (normal, NORMAL))
	zlog_debug ("%s bad OPEN, remote AS is %u, expected %u",
		   peer->host, remote_as, peer->as);
      bgp_notify_send_with_data (peer, 
				 BGP_NOTIFY_OPEN_ERR, 
				 BGP_NOTIFY_OPEN_BAD_PEER_AS,
				 notify_data_remote_as, 2);
      return -1;
    }

  /* From the rfc: Upon receipt of an OPEN message, a BGP speaker MUST
     calculate the value of the Hold Timer by using the smaller of its
     configured Hold Time and the Hold Time received in the OPEN message.
     The Hold Time MUST be either zero or at least three seconds.  An
     implementation may reject connections on the basis of the Hold Time. */

  if (holdtime < 3 && holdtime != 0)
    {
      uint16_t netholdtime = htons (holdtime);
      bgp_notify_send_with_data (peer,
		                 BGP_NOTIFY_OPEN_ERR,
		                 BGP_NOTIFY_OPEN_UNACEP_HOLDTIME,
                                 (u_int8_t *) &netholdtime, 2);
      return -1;
    }
    
  /* From the rfc: A reasonable maximum time between KEEPALIVE messages
     would be one third of the Hold Time interval.  KEEPALIVE messages
     MUST NOT be sent more frequently than one per second.  An
     implementation MAY adjust the rate at which it sends KEEPALIVE
     messages as a function of the Hold Time interval. */

  if (CHECK_FLAG (peer->config, PEER_CONFIG_TIMER))
    send_holdtime = peer->holdtime;
  else
    send_holdtime = peer->bgp->default_holdtime;

  if (holdtime < send_holdtime)
    peer->v_holdtime = holdtime;
  else
    peer->v_holdtime = send_holdtime;

  peer->v_keepalive = peer->v_holdtime / 3;

  /* Open option part parse. */
  if (optlen != 0) 
    {
      if ((ret = bgp_open_option_parse (peer, optlen, &mp_capability)) < 0)
        {
          bgp_notify_send (peer,
                 BGP_NOTIFY_OPEN_ERR,
                 BGP_NOTIFY_OPEN_UNSPECIFIC);
	  return ret;
        }
    }
  else
    {
      if (BGP_DEBUG (normal, NORMAL))
	zlog_debug ("%s rcvd OPEN w/ OPTION parameter len: 0",
		   peer->host);
    }

  /* 
   * Assume that the peer supports the locally configured set of
   * AFI/SAFIs if the peer did not send us any Mulitiprotocol
   * capabilities, or if 'override-capability' is configured.
   */
  if (! mp_capability ||
      CHECK_FLAG (peer->flags, PEER_FLAG_OVERRIDE_CAPABILITY))
    {
      peer->afc_nego[AFI_IP][SAFI_UNICAST] = peer->afc[AFI_IP][SAFI_UNICAST];
      peer->afc_nego[AFI_IP][SAFI_MULTICAST] = peer->afc[AFI_IP][SAFI_MULTICAST];
      peer->afc_nego[AFI_IP6][SAFI_UNICAST] = peer->afc[AFI_IP6][SAFI_UNICAST];
      peer->afc_nego[AFI_IP6][SAFI_MULTICAST] = peer->afc[AFI_IP6][SAFI_MULTICAST];
    }

  /* Get sockname. */
  bgp_getsockname (peer);
  peer->rtt = sockopt_tcp_rtt (peer->fd);

  BGP_EVENT_ADD (peer, Receive_OPEN_message);

  peer->packet_size = 0;
  if (peer->ibuf)
    stream_reset (peer->ibuf);

  return 0;
}

/* Frontend for NLRI parsing, to fan-out to AFI/SAFI specific parsers */
int
bgp_nlri_parse (struct peer *peer, struct attr *attr, struct bgp_nlri *packet,uint32_t *passed_root_cause_event_id,char *passed_time_stamp,uint32_t *passed_owner_id)
{


    // char rec_time_stamp_for_this_function[50]; 
   
    // sprintf(rec_time_stamp_for_this_function, "%u", passed_time_stamp); 




   // zlog_debug ("we are going to call bgp_nlri_parse_ip for timestamp %s ",passed_time_stamp);

  switch (packet->safi)
    {
      case SAFI_UNICAST:
      case SAFI_MULTICAST:
        return bgp_nlri_parse_ip (peer, attr, packet,passed_root_cause_event_id,passed_time_stamp,passed_owner_id);
      case SAFI_MPLS_VPN:
      case SAFI_MPLS_LABELED_VPN:
        return bgp_nlri_parse_vpn (peer, attr, packet);
      case SAFI_ENCAP:
        return bgp_nlri_parse_encap (peer, attr, packet);
    }
  return -1;
}




/* Parse BGP Update packet and make attribute object. */
static int
bgp_update_receive (struct peer *peer, bgp_size_t size)
{


  uint32_t rec_time_stamp ;

  uint32_t rec_root_cause_event_id ;

  uint32_t rec_root_cause_event_owner_router_id ;



  // struct peer *peer_for_being_used_for_head_in_receive;

  // /* Allocate new peer. */
  // peer_for_being_used_for_head_in_receive = XCALLOC (MTYPE_BGP_PEER, sizeof (struct peer));
  // peer_for_being_used_for_head_in_receive -> pref_neigh_pair = NULL;
  // peer_for_being_used_for_head_in_receive -> received_prefix = NULL;

  // struct peer* temp = (struct peer *) malloc(sizeof(struct peer));
  // temp -> local_as = 1111;
  // //this is where I initialize the data structure
 
  // //adding a fake entry to the data structure
  // add_to_received_prefix(&(peer -> received_prefix), "10.10.10.10", "123,45", temp, 5155);
  
  // struct peer * check_peer = (struct peer *) malloc(sizeof(struct peer));
  // check_peer -> local_as = 1111;
  // check_peer -> host = "host 6565";
  // add_to_prefix_neighbour_pair(&(peer -> pref_neigh_pair ), "10.10.10.10", check_peer);






  int ret, nlri_ret;
  u_char *end;
  struct stream *s;
  struct attr attr;
  struct attr_extra extra;
  bgp_size_t attribute_len;
  bgp_size_t update_len;
  bgp_size_t withdraw_len;
  int i;
  
  enum NLRI_TYPES {
    NLRI_UPDATE,
    NLRI_WITHDRAW,
    NLRI_MP_UPDATE,
    NLRI_MP_WITHDRAW,
    NLRI_TYPE_MAX,
  };
  struct bgp_nlri nlris[NLRI_TYPE_MAX];

  /* Status must be Established. */
  if (peer->status != Established) 
    {
      zlog_err ("%s [FSM] Update packet received under status %s",
		peer->host, LOOKUP (bgp_status_msg, peer->status));
      bgp_notify_send (peer, BGP_NOTIFY_FSM_ERR, 0);
      return -1;
    }

  /* Set initial values. */
  memset (&attr, 0, sizeof (struct attr));
  memset (&extra, 0, sizeof (struct attr_extra));
  memset (&nlris, 0, sizeof nlris);

  attr.extra = &extra;

  s = peer->ibuf;



      char result3[50]; 
   
    sprintf(result3, "%u", stream_pnt (s)); 

    //zlog_debug ("%s 1. this is the stream_pnt (s) ", result3);


      char result4[50]; 
   
    sprintf(result4, "%u", size); 

    //zlog_debug ("%s 2. this is the size ", result4);


  end = stream_pnt (s) + size;


      char result2[50]; 
   
    sprintf(result2, "%u", end); 

    //zlog_debug ("%s 3.this is the end  ", result2);

  if (  size == 4 )
    {
      
      return -1;
    }

  /* RFC1771 6.3 If the Unfeasible Routes Length or Total Attribute
     Length is too large (i.e., if Unfeasible Routes Length + Total
     Attribute Length + 23 exceeds the message Length), then the Error
     Subcode is set to Malformed Attribute List.  */
  if (stream_pnt (s) + 2 > end)
    {
      zlog_err ("%s [Error] Update packet error"
		" (packet length is short for unfeasible length)",
		peer->host);
      bgp_notify_send (peer, BGP_NOTIFY_UPDATE_ERR, 
		       BGP_NOTIFY_UPDATE_MAL_ATTR);
      return -1;
    }



    int size_of_stream2 = s->size;

    char char_size_of_stream2[50]; 
   
    sprintf(char_size_of_stream2, "%u", size_of_stream2); 

    //zlog_debug ("%s this is size_of_stream", char_size_of_stream2);


    int end_of_s2 = s->endp;

    char char_end_of_s2[50]; 
   
    sprintf(char_end_of_s2, "%u", end_of_s2); 

   // zlog_debug ("%s this is end_of_s", char_end_of_s2);





  /* get root cause evnt id. */



  rec_time_stamp = stream_getl (s);

  rec_root_cause_event_id = stream_getl (s);

  rec_root_cause_event_owner_router_id = stream_getl (s);
// if (time_stamp == 111111)
//     zlog_debug ("this is the timestamp");

// if (root_cause_event_id == 1234567)
//     zlog_debug ("this is the root_cause_event_id");

  char *char_rec_time_stamp[50];
  sprintf( char_rec_time_stamp, "%u", rec_time_stamp );

  char char_rec_root_cause_event_id[50];
  sprintf( char_rec_root_cause_event_id, "%u", rec_root_cause_event_id );

    char char_rec_root_cause_event_owner_router_id[50];
  sprintf( char_rec_root_cause_event_owner_router_id, "%u", rec_root_cause_event_owner_router_id );




  // char str3[50];
  // uint32_t my_num;
  // my_num = 1234567;
  // sprintf( str3, "%u",  my_num);

  // zlog_debug ("%s this is the 1234567", str3);


  // zlog_debug ("%s 3.this is received time_stamp", char_rec_time_stamp);

  // zlog_debug ("%s 4.this is the received_root_cause_event_id", char_rec_root_cause_event_id);

  // zlog_debug ("%s 4.this is the received_root_cause_event_owner", char_rec_root_cause_event_owner_router_id);

  /* Unfeasible Route Length. */
  withdraw_len = stream_getw (s);




      char result5[50]; 
   
    sprintf(result5, "%u", withdraw_len); 

    char result35[50]; 
   
    sprintf(result35, "%u", stream_pnt (s)); 


    char char_end_of_s22[50]; 
   
    sprintf(char_end_of_s22, "%u", end); 

   // zlog_debug ("52. this is the withdraw_len %s and stream_pnt (s) %s and > end %s", result5,result35,char_end_of_s22);






  /* Unfeasible Route Length check. */
  if (stream_pnt (s) + withdraw_len > end)
    {
      zlog_err ("%s [Error] Update packet error"
		" (packet unfeasible length overflow %d)",
		peer->host, withdraw_len);
      bgp_notify_send (peer, BGP_NOTIFY_UPDATE_ERR, 
		       BGP_NOTIFY_UPDATE_MAL_ATTR);
      return -1;
    }

  /* Unfeasible Route packet format check. */
  if (withdraw_len > 0)
    {
      nlris[NLRI_WITHDRAW].afi = AFI_IP;
      nlris[NLRI_WITHDRAW].safi = SAFI_UNICAST;
      nlris[NLRI_WITHDRAW].nlri = stream_pnt (s);
      nlris[NLRI_WITHDRAW].length = withdraw_len;
      
      if (BGP_DEBUG (packet, PACKET_RECV))
	zlog_debug ("%s [Update:RECV] Unfeasible NLRI received", peer->host);

      stream_forward_getp (s, withdraw_len);
    }
  
  /* Attribute total length check. */


    char result1[50]; 
   
    sprintf(result1, "%u", stream_pnt (s)); 

   // zlog_debug ("%s 6. this is the stream_pnt (s) ", result1);




  if (stream_pnt (s)  == end )
    {
      
      return -1;
    }


  if (stream_pnt (s) + 4 > end && 1==2)
    {
      zlog_warn ("%s [Error] Packet Error"
		 " (update packet is short for attribute length)",
		 peer->host);
      bgp_notify_send (peer, BGP_NOTIFY_UPDATE_ERR, 
		       BGP_NOTIFY_UPDATE_MAL_ATTR);
      return -1;
    }





  /* Fetch attribute total length. */
  attribute_len = stream_getw (s);




      char result6[50]; 
   
    sprintf(result6, "%u", attribute_len); 

   // zlog_debug ("%s 7. this is the attribute_len ", result6);





  /* Attribute length check. */
  if (stream_pnt (s) + attribute_len > end)
    {
      zlog_warn ("%s [Error] Packet Error"
		 " (update packet attribute length overflow %d)",
		 peer->host, attribute_len);
      bgp_notify_send (peer, BGP_NOTIFY_UPDATE_ERR, 
		       BGP_NOTIFY_UPDATE_MAL_ATTR);
      return -1;
    }
  
  /* Certain attribute parsing errors should not be considered bad enough
   * to reset the session for, most particularly any partial/optional
   * attributes that have 'tunneled' over speakers that don't understand
   * them. Instead we withdraw only the prefix concerned.
   * 
   * Complicates the flow a little though..
   */
  bgp_attr_parse_ret_t attr_parse_ret = BGP_ATTR_PARSE_PROCEED;
  /* This define morphs the update case into a withdraw when lower levels
   * have signalled an error condition where this is best.
   */
#define NLRI_ATTR_ARG (attr_parse_ret != BGP_ATTR_PARSE_WITHDRAW ? &attr : NULL)

  /* Parse attribute when it exists. */
  if (attribute_len)
    {
      attr_parse_ret = bgp_attr_parse (peer, &attr, attribute_len, 
			    &nlris[NLRI_MP_UPDATE], &nlris[NLRI_MP_WITHDRAW]);
      if (attr_parse_ret == BGP_ATTR_PARSE_ERROR)
	{
	  bgp_attr_unintern_sub (&attr);
          bgp_attr_flush (&attr);
	  return -1;
	}
    }
  

 //zlog_debug ("%s this is the main function for BGP update receiving messages", "----------------------");

  /* Logging the attribute. */
  if (attr_parse_ret == BGP_ATTR_PARSE_WITHDRAW
      || BGP_DEBUG (update, UPDATE_IN))
    {
      char attrstr[BUFSIZ];
      attrstr[0] = '\0';

      ret= bgp_dump_attr (peer, &attr, attrstr, BUFSIZ);
      int lvl = (attr_parse_ret == BGP_ATTR_PARSE_WITHDRAW)
                 ? LOG_ERR : LOG_DEBUG;
      
      if (attr_parse_ret == BGP_ATTR_PARSE_WITHDRAW)
        zlog (peer->log, LOG_ERR,
              "%s rcvd UPDATE with errors in attr(s)!! Withdrawing route.",
              peer->host);

      if (ret)
      {
      //zlog_debug ("%s Here we have our community attributes ",attrstr);


	zlog (peer->log, lvl, "%s rcvd UPDATE w/ attr: %s",
	      peer->host, attrstr);

      // Here I am going to send back the fizzle message...
  //zlog_debug ("%s We are going to set  timer  line 176 for bgp_holdtime_timer...... to ", "Shahrooz");

      //zlog_debug ("Here I am going to send my fizzle message");


      // first check if the receive update message has changed our best route or not
      //bgp_fizzle_send (peer);
      }
        
    }
  
  /* Network Layer Reachability Information. */
  update_len = end - stream_pnt (s);


   // zlog_debug ("%s and we are goign to do more", "----------------------");


  if (update_len)
    {
      //zlog_debug ("%s we do not have update_len ","................");

      /* Set NLRI portion to structure. */
      nlris[NLRI_UPDATE].afi = AFI_IP;
      nlris[NLRI_UPDATE].safi = SAFI_UNICAST;
      nlris[NLRI_UPDATE].nlri = stream_pnt (s);
      nlris[NLRI_UPDATE].length = update_len;
      
      stream_forward_getp (s, update_len);
    }
  
  /* Parse any given NLRIs */
  for (i = NLRI_UPDATE; i < NLRI_TYPE_MAX; i++)
    {      


      //zlog_debug ("%s we are in a loop ","-----------------");


      if (!nlris[i].nlri) continue;
      
      /* We use afi and safi as indices into tables and what not.  It would
       * be impossible, at this time, to support unknown afi/safis.  And
       * anyway, the peer needs to be configured to enable the afi/safi
       * explicitly which requires UI support.
       *
       * Ignore unknown afi/safi NLRIs.
       *
       * Note: this means nlri[x].afi/safi still can not be trusted for
       * indexing later in this function!
       *
       * Note2: This will also remap the wire code-point for VPN safi to the
       * internal safi_t point, as needs be.
       */
      if (!bgp_afi_safi_valid_indices (nlris[i].afi, &nlris[i].safi))
        {
          plog_info (peer->log,
                     "%s [Info] UPDATE with unsupported AFI/SAFI %u/%u",
                     peer->host, nlris[i].afi, nlris[i].safi);
          continue;
        }
      
      /* NLRI is processed only when the peer is configured specific
         Address Family and Subsequent Address Family. */
      if (!peer->afc[nlris[i].afi][nlris[i].safi])
        {
          plog_info (peer->log,
                     "%s [Info] UPDATE for non-enabled AFI/SAFI %u/%u",
                     peer->host, nlris[i].afi, nlris[i].safi);
          continue;
        }
      
      /* EoR handled later */
      if (nlris[i].length == 0)
        continue;
      
      switch (i)
        {
          case NLRI_UPDATE:
          case NLRI_MP_UPDATE:
           // zlog_debug ("Lets pars all received prefixes in this update messages from %s ",peer->host);
           // zlog_debug ("Lets pars all received prefixes in this update messages for time stamp %s ",char_rec_time_stamp);

            nlri_ret = bgp_nlri_parse (peer, NLRI_ATTR_ARG, &nlris[i],rec_root_cause_event_id,char_rec_time_stamp,rec_root_cause_event_owner_router_id);
           // zlog_debug ("we parsed all received prefixes in this update messages from %s ",peer->host);

            break;
          case NLRI_WITHDRAW:
          case NLRI_MP_WITHDRAW:
            nlri_ret = bgp_nlri_parse (peer, NULL, &nlris[i],rec_root_cause_event_id,char_rec_time_stamp,rec_root_cause_event_owner_router_id);
        }
      
      if (nlri_ret < 0)
        {
          plog_err (peer->log, 
                    "%s [Error] Error parsing NLRI", peer->host);
          if (peer->status == Established)
            bgp_notify_send (peer, BGP_NOTIFY_UPDATE_ERR,
                             i <= NLRI_WITHDRAW 
                               ? BGP_NOTIFY_UPDATE_INVAL_NETWORK
                               : BGP_NOTIFY_UPDATE_OPT_ATTR_ERR);
          bgp_attr_unintern_sub (&attr);
          return -1;
        }
    }
  
  /* EoR checks.
   *
   * Non-MP IPv4/Unicast EoR is a completely empty UPDATE
   * and MP EoR should have only an empty MP_UNREACH
   */
  if (!update_len && !withdraw_len
      && nlris[NLRI_MP_UPDATE].length == 0)
    {


    //zlog_debug ("%s we are in if (!update_len && !withdraw_len ","--------............---------");



      afi_t afi = 0;
      safi_t safi;
      
      /* Non-MP IPv4/Unicast is a completely empty UPDATE - already
       * checked update and withdraw NLRI lengths are 0.
       */ 
      if (!attribute_len)
        {

       // zlog_debug ("%s we are in if (!attribute_len) ","--------............---------");


          afi = AFI_IP;
          safi = SAFI_UNICAST;
        }
      /* otherwise MP AFI/SAFI is an empty update, other than an empty
       * MP_UNREACH_NLRI attr (with an AFI/SAFI we recognise).
       */
      else if (attr.flag == BGP_ATTR_MP_UNREACH_NLRI
               && nlris[NLRI_MP_WITHDRAW].length == 0
               && bgp_afi_safi_valid_indices (nlris[NLRI_MP_WITHDRAW].afi,
                                              &nlris[NLRI_MP_WITHDRAW].safi))
        {

                  //zlog_debug ("%s we are in else if (attr.flag == BGP_ATTR_MP_UNREACH_NLRI ","--------............---------");

          afi = nlris[NLRI_MP_WITHDRAW].afi;
          safi = nlris[NLRI_MP_WITHDRAW].safi;
        }
      
      if (afi && peer->afc[afi][safi])
        {
      //zlog_debug ("%s we are in if (afi && peer->afc[afi][safi]) ","--------............---------");


	  /* End-of-RIB received */
	  SET_FLAG (peer->af_sflags[afi][safi],
		    PEER_STATUS_EOR_RECEIVED);

	  /* NSF delete stale route */
	  if (peer->nsf[afi][safi])
    {

     // zlog_debug ("%s we are in NSF delete stale route","--------............---------");

	    bgp_clear_stale_route (peer, afi, safi);
    }

	  if (BGP_DEBUG (normal, NORMAL))
	    zlog (peer->log, LOG_DEBUG, "rcvd End-of-RIB for %s from %s",
		  peer->host, afi_safi_print (afi, safi));
        }
    }
  
  /* Everything is done.  We unintern temporary structures which
     interned in bgp_attr_parse(). */
  bgp_attr_unintern_sub (&attr);
  bgp_attr_flush (&attr);

  /* If peering is stopped due to some reason, do not generate BGP
     event.  */
  if (peer->status != Established)
    return 0;

  /* Increment packet counter. */
  peer->update_in++;
  peer->update_time = bgp_clock ();

  /* Rearm holdtime timer */
  BGP_TIMER_OFF (peer->t_holdtime);
  bgp_timer_set (peer);


    //zlog_debug ("%s this update finished", "-------------........---------");


  //zlog_debug ("%s this is the value of to_be_sent_peers list", to_be_sent_peers);


  //zlog_debug ("%s this is the value of waiting_peers list", waiting_peers);
  // if (to_be_sent_peers->host)
  // {
  //   if(waiting_peers->host)
  //   {
  //     zlog_debug ("we need to send back a fizzle to %s ", waiting_peers->host);
  //   }
  // }


  return 0;
}

/* Notify message treatment function. */
static void
bgp_notify_receive (struct peer *peer, bgp_size_t size)
{
  struct bgp_notify bgp_notify;

  if (peer->notify.data)
    {
      XFREE (MTYPE_TMP, peer->notify.data);
      peer->notify.data = NULL;
      peer->notify.length = 0;
    }

  bgp_notify.code = stream_getc (peer->ibuf);
  bgp_notify.subcode = stream_getc (peer->ibuf);
  bgp_notify.length = size - 2;
  bgp_notify.data = NULL;

  /* Preserv notify code and sub code. */
  peer->notify.code = bgp_notify.code;
  peer->notify.subcode = bgp_notify.subcode;
  /* For further diagnostic record returned Data. */
  if (bgp_notify.length)
    {
      peer->notify.length = size - 2;
      peer->notify.data = XMALLOC (MTYPE_TMP, size - 2);
      memcpy (peer->notify.data, stream_pnt (peer->ibuf), size - 2);
    }

  /* For debug */
  {
    int i;
    int first = 0;
    char c[4];

    if (bgp_notify.length)
      {
	bgp_notify.data = XMALLOC (MTYPE_TMP, bgp_notify.length * 3);
	for (i = 0; i < bgp_notify.length; i++)
	  if (first)
	    {
	      sprintf (c, " %02x", stream_getc (peer->ibuf));
	      strcat (bgp_notify.data, c);
	    }
	  else
	    {
	      first = 1;
	      sprintf (c, "%02x", stream_getc (peer->ibuf));
	      strcpy (bgp_notify.data, c);
	    }
      }

    bgp_notify_print(peer, &bgp_notify, "received");
    if (bgp_notify.data)
      {
        XFREE (MTYPE_TMP, bgp_notify.data);
        bgp_notify.data = NULL;
        bgp_notify.length = 0;
      }
  }

  /* peer count update */
  peer->notify_in++;

  if (peer->status == Established)
    peer->last_reset = PEER_DOWN_NOTIFY_RECEIVED;

  /* We have to check for Notify with Unsupported Optional Parameter.
     in that case we fallback to open without the capability option.
     But this done in bgp_stop. We just mark it here to avoid changing
     the fsm tables.  */
  if (bgp_notify.code == BGP_NOTIFY_OPEN_ERR &&
      bgp_notify.subcode == BGP_NOTIFY_OPEN_UNSUP_PARAM )
    UNSET_FLAG (peer->sflags, PEER_STATUS_CAPABILITY_OPEN);

  BGP_EVENT_ADD (peer, Receive_NOTIFICATION_message);
}

/* Keepalive treatment function -- get keepalive send keepalive */
static void
bgp_keepalive_receive (struct peer *peer, bgp_size_t size)
{

  //zlog_debug ("%s We received keep alive message .... ", peer->host);

  if (BGP_DEBUG (keepalive, KEEPALIVE))  
    zlog_debug ("%s KEEPALIVE rcvd", peer->host); 
  
  BGP_EVENT_ADD (peer, Receive_KEEPALIVE_message);
}

/* Route refresh message is received. */
static void
bgp_route_refresh_receive (struct peer *peer, bgp_size_t size)
{
  afi_t afi;
  safi_t safi;
  struct stream *s;

  /* If peer does not have the capability, send notification. */
  if (! CHECK_FLAG (peer->cap, PEER_CAP_REFRESH_ADV))
    {
      plog_err (peer->log, "%s [Error] BGP route refresh is not enabled",
		peer->host);
      bgp_notify_send (peer,
		       BGP_NOTIFY_HEADER_ERR,
		       BGP_NOTIFY_HEADER_BAD_MESTYPE);
      return;
    }

  /* Status must be Established. */
  if (peer->status != Established) 
    {
      plog_err (peer->log,
		"%s [Error] Route refresh packet received under status %s",
		peer->host, LOOKUP (bgp_status_msg, peer->status));
      bgp_notify_send (peer, BGP_NOTIFY_FSM_ERR, 0);
      return;
    }

  s = peer->ibuf;
  
  /* Parse packet. */
  afi = stream_getw (s);
  /* reserved byte */
  stream_getc (s);
  safi = stream_getc (s);

  if (BGP_DEBUG (normal, NORMAL))
    zlog_debug ("%s rcvd REFRESH_REQ for afi/safi: %d/%d",
	       peer->host, afi, safi);

  /* Check AFI and SAFI. */
  if ((afi != AFI_IP && afi != AFI_IP6)
      || (safi != SAFI_UNICAST && safi != SAFI_MULTICAST
	  && safi != SAFI_MPLS_LABELED_VPN))
    {
      if (BGP_DEBUG (normal, NORMAL))
	{
	  zlog_debug ("%s REFRESH_REQ for unrecognized afi/safi: %d/%d - ignored",
		     peer->host, afi, safi);
	}
      return;
    }

  /* Adjust safi code. */
  if (safi == SAFI_MPLS_LABELED_VPN)
    safi = SAFI_MPLS_VPN;

  if (size != BGP_MSG_ROUTE_REFRESH_MIN_SIZE - BGP_HEADER_SIZE)
    {
      u_char *end;
      u_char when_to_refresh;
      u_char orf_type;
      u_int16_t orf_len;

      if (size - (BGP_MSG_ROUTE_REFRESH_MIN_SIZE - BGP_HEADER_SIZE) < 5)
        {
          zlog_info ("%s ORF route refresh length error", peer->host);
          bgp_notify_send (peer, BGP_NOTIFY_CEASE, 0);
          return;
        }

      when_to_refresh = stream_getc (s);
      end = stream_pnt (s) + (size - 5);

      while ((stream_pnt (s) + 2) < end)
	{
	  orf_type = stream_getc (s); 
	  orf_len = stream_getw (s);
	  
	  /* orf_len in bounds? */
	  if ((stream_pnt (s) + orf_len) > end)
	    break; /* XXX: Notify instead?? */
	  if (orf_type == ORF_TYPE_PREFIX
	      || orf_type == ORF_TYPE_PREFIX_OLD)
	    {
	      uint8_t *p_pnt = stream_pnt (s);
	      uint8_t *p_end = stream_pnt (s) + orf_len;
	      struct orf_prefix orfp;
	      u_char common = 0;
	      u_int32_t seq;
	      int psize;
	      char name[BUFSIZ];
	      int ret;

	      if (BGP_DEBUG (normal, NORMAL))
		{
		  zlog_debug ("%s rcvd Prefixlist ORF(%d) length %d",
			     peer->host, orf_type, orf_len);
		}

              /* we're going to read at least 1 byte of common ORF header,
               * and 7 bytes of ORF Address-filter entry from the stream
               */
              if (orf_len < 7)
                break; 
                
	      /* ORF prefix-list name */
	      sprintf (name, "%s.%d.%d", peer->host, afi, safi);

	      while (p_pnt < p_end)
		{
                  /* If the ORF entry is malformed, want to read as much of it
                   * as possible without going beyond the bounds of the entry,
                   * to maximise debug information.
                   */
		  int ok;
		  memset (&orfp, 0, sizeof (struct orf_prefix));
		  common = *p_pnt++;
		  /* after ++: p_pnt <= p_end */
		  if (common & ORF_COMMON_PART_REMOVE_ALL)
		    {
		      if (BGP_DEBUG (normal, NORMAL))
			zlog_debug ("%s rcvd Remove-All pfxlist ORF request", peer->host);
		      prefix_bgp_orf_remove_all (afi, name);
		      break;
		    }
		  ok = ((size_t)(p_end - p_pnt) >= sizeof(u_int32_t)) ;
		  if (ok)
		    {
		      memcpy (&seq, p_pnt, sizeof (u_int32_t));
                      p_pnt += sizeof (u_int32_t);
                      orfp.seq = ntohl (seq);
		    }
		  else
		    p_pnt = p_end ;

		  if ((ok = (p_pnt < p_end)))
		    orfp.ge = *p_pnt++ ;      /* value checked in prefix_bgp_orf_set() */
		  if ((ok = (p_pnt < p_end)))
		    orfp.le = *p_pnt++ ;      /* value checked in prefix_bgp_orf_set() */
		  if ((ok = (p_pnt < p_end)))
		    orfp.p.prefixlen = *p_pnt++ ;
		  orfp.p.family = afi2family (afi);   /* afi checked already  */

		  psize = PSIZE (orfp.p.prefixlen);   /* 0 if not ok          */
		  if (psize > prefix_blen(&orfp.p))   /* valid for family ?   */
		    {
		      ok = 0 ;
		      psize = prefix_blen(&orfp.p) ;
		    }
		  if (psize > (p_end - p_pnt))        /* valid for packet ?   */
		    {
		      ok = 0 ;
		      psize = p_end - p_pnt ;
		    }

		  if (psize > 0)
		    memcpy (&orfp.p.u.prefix, p_pnt, psize);
		  p_pnt += psize;

		  if (BGP_DEBUG (normal, NORMAL))
		    {
		      char buf[INET6_BUFSIZ];

		      zlog_debug ("%s rcvd %s %s seq %u %s/%d ge %d le %d%s",
			         peer->host,
			         (common & ORF_COMMON_PART_REMOVE ? "Remove" : "Add"),
			         (common & ORF_COMMON_PART_DENY ? "deny" : "permit"),
			         orfp.seq,
			         inet_ntop (orfp.p.family, &orfp.p.u.prefix, buf, INET6_BUFSIZ),
			         orfp.p.prefixlen, orfp.ge, orfp.le,
			         ok ? "" : " MALFORMED");
		    }

		  if (ok)
		    ret = prefix_bgp_orf_set (name, afi, &orfp,
				   (common & ORF_COMMON_PART_DENY ? 0 : 1 ),
				   (common & ORF_COMMON_PART_REMOVE ? 0 : 1));
                  
		  if (!ok || (ok && ret != CMD_SUCCESS))
		    {
		      if (BGP_DEBUG (normal, NORMAL))
			zlog_debug ("%s Received misformatted prefixlist ORF."
			            " Remove All pfxlist", peer->host);
		      prefix_bgp_orf_remove_all (afi, name);
		      break;
		    }
		}
	      peer->orf_plist[afi][safi] =
			 prefix_bgp_orf_lookup (afi, name);
	    }
	  stream_forward_getp (s, orf_len);
	}
      if (BGP_DEBUG (normal, NORMAL))
	zlog_debug ("%s rcvd Refresh %s ORF request", peer->host,
		   when_to_refresh == REFRESH_DEFER ? "Defer" : "Immediate");
      if (when_to_refresh == REFRESH_DEFER)
	return;
    }

  /* First update is deferred until ORF or ROUTE-REFRESH is received */
  if (CHECK_FLAG (peer->af_sflags[afi][safi], PEER_STATUS_ORF_WAIT_REFRESH))
    UNSET_FLAG (peer->af_sflags[afi][safi], PEER_STATUS_ORF_WAIT_REFRESH);

  /* Perform route refreshment to the peer */
  bgp_announce_route (peer, afi, safi);
}

static int
bgp_capability_msg_parse (struct peer *peer, u_char *pnt, bgp_size_t length)
{
  u_char *end;
  struct capability_mp_data mpc;
  struct capability_header *hdr;
  u_char action;
  afi_t afi;
  safi_t safi;

  end = pnt + length;

  /* XXX: Streamify this */
  for (; pnt < end; pnt += hdr->length + 3)
    {      
      /* We need at least action, capability code and capability length. */
      if (pnt + 3 > end)
        {
          zlog_info ("%s Capability length error", peer->host);
          bgp_notify_send (peer, BGP_NOTIFY_CEASE, 0);
          return -1;
        }
      action = *pnt;
      hdr = (struct capability_header *)(pnt + 1);
      
      /* Action value check.  */
      if (action != CAPABILITY_ACTION_SET
	  && action != CAPABILITY_ACTION_UNSET)
        {
          zlog_info ("%s Capability Action Value error %d",
		     peer->host, action);
          bgp_notify_send (peer, BGP_NOTIFY_CEASE, 0);
          return -1;
        }

      if (BGP_DEBUG (normal, NORMAL))
	zlog_debug ("%s CAPABILITY has action: %d, code: %u, length %u",
		   peer->host, action, hdr->code, hdr->length);

      /* Capability length check. */
      if ((pnt + hdr->length + 3) > end)
        {
          zlog_info ("%s Capability length error", peer->host);
          bgp_notify_send (peer, BGP_NOTIFY_CEASE, 0);
          return -1;
        }

      /* Fetch structure to the byte stream. */
      memcpy (&mpc, pnt + 3, sizeof (struct capability_mp_data));

      /* We know MP Capability Code. */
      if (hdr->code == CAPABILITY_CODE_MP)
        {
	  afi = ntohs (mpc.afi);
	  safi = mpc.safi;

          /* Ignore capability when override-capability is set. */
          if (CHECK_FLAG (peer->flags, PEER_FLAG_OVERRIDE_CAPABILITY))
	    continue;
          
          if (!bgp_afi_safi_valid_indices (afi, &safi))
            {
              if (BGP_DEBUG (normal, NORMAL))
                zlog_debug ("%s Dynamic Capability MP_EXT afi/safi invalid "
                            "(%u/%u)", peer->host, afi, safi);
              continue;
            }
          
	  /* Address family check.  */
          if (BGP_DEBUG (normal, NORMAL))
            zlog_debug ("%s CAPABILITY has %s MP_EXT CAP for afi/safi: %u/%u",
                       peer->host,
                       action == CAPABILITY_ACTION_SET 
                       ? "Advertising" : "Removing",
                       ntohs(mpc.afi) , mpc.safi);
              
          if (action == CAPABILITY_ACTION_SET)
            {
              peer->afc_recv[afi][safi] = 1;
              if (peer->afc[afi][safi])
                {
                  peer->afc_nego[afi][safi] = 1;
                  bgp_announce_route (peer, afi, safi);
                }
            }
          else
            {
              peer->afc_recv[afi][safi] = 0;
              peer->afc_nego[afi][safi] = 0;

              if (peer_active_nego (peer))
                bgp_clear_route (peer, afi, safi, BGP_CLEAR_ROUTE_NORMAL);
              else
                BGP_EVENT_ADD (peer, BGP_Stop);
            }
        }
      else
        {
          zlog_warn ("%s unrecognized capability code: %d - ignored",
                     peer->host, hdr->code);
        }
    }
  return 0;
}

/* Dynamic Capability is received. 
 *
 * This is exported for unit-test purposes
 */
int
bgp_capability_receive (struct peer *peer, bgp_size_t size)
{
  u_char *pnt;

  /* Fetch pointer. */
  pnt = stream_pnt (peer->ibuf);

  if (BGP_DEBUG (normal, NORMAL))
    zlog_debug ("%s rcv CAPABILITY", peer->host);

  /* If peer does not have the capability, send notification. */
  if (! CHECK_FLAG (peer->cap, PEER_CAP_DYNAMIC_ADV))
    {
      plog_err (peer->log, "%s [Error] BGP dynamic capability is not enabled",
		peer->host);
      bgp_notify_send (peer,
		       BGP_NOTIFY_HEADER_ERR,
		       BGP_NOTIFY_HEADER_BAD_MESTYPE);
      return -1;
    }

  /* Status must be Established. */
  if (peer->status != Established)
    {
      plog_err (peer->log,
		"%s [Error] Dynamic capability packet received under status %s", peer->host, LOOKUP (bgp_status_msg, peer->status));
      bgp_notify_send (peer, BGP_NOTIFY_FSM_ERR, 0);
      return -1;
    }

  /* Parse packet. */
  return bgp_capability_msg_parse (peer, pnt, size);
}

/* BGP read utility function. */
static int
bgp_read_packet (struct peer *peer)
{
  int nbytes;
  int readsize;

  readsize = peer->packet_size - stream_get_endp (peer->ibuf);

  /* If size is zero then return. */
  if (! readsize)
    return 0;

  /* Read packet from fd. */
  nbytes = stream_read_try (peer->ibuf, peer->fd, readsize);

  /* If read byte is smaller than zero then error occurred. */
  if (nbytes < 0) 
    {
      /* Transient error should retry */
      if (nbytes == -2)
	return -1;

      plog_err (peer->log, "%s [Error] bgp_read_packet error: %s",
		 peer->host, safe_strerror (errno));

      if (peer->status == Established) 
	{
	  if (CHECK_FLAG (peer->sflags, PEER_STATUS_NSF_MODE))
	    {
	      peer->last_reset = PEER_DOWN_NSF_CLOSE_SESSION;
	      SET_FLAG (peer->sflags, PEER_STATUS_NSF_WAIT);
	    }
	  else
	    peer->last_reset = PEER_DOWN_CLOSE_SESSION;
	}

      BGP_EVENT_ADD (peer, TCP_fatal_error);
      return -1;
    }  

  /* When read byte is zero : clear bgp peer and return */
  if (nbytes == 0) 
    {
      if (BGP_DEBUG (events, EVENTS))
	plog_debug (peer->log, "%s [Event] BGP connection closed fd %d",
		   peer->host, peer->fd);

      if (peer->status == Established) 
	{
	  if (CHECK_FLAG (peer->sflags, PEER_STATUS_NSF_MODE))
	    {
	      peer->last_reset = PEER_DOWN_NSF_CLOSE_SESSION;
	      SET_FLAG (peer->sflags, PEER_STATUS_NSF_WAIT);
	    }
	  else
	    peer->last_reset = PEER_DOWN_CLOSE_SESSION;
	}

      BGP_EVENT_ADD (peer, TCP_connection_closed);
      return -1;
    }

  /* We read partial packet. */
  if (stream_get_endp (peer->ibuf) != peer->packet_size)
    return -1;

  return 0;
}

/* Marker check. */
static int
bgp_marker_all_one (struct stream *s, int length)
{
  int i;

  for (i = 0; i < length; i++)
    if (s->data[i] != 0xff)
      return 0;

  return 1;
}

/* Recent thread time.
   On same clock base as bgp_clock (MONOTONIC)
   but can be time of last context switch to bgp_read thread. */
static time_t
bgp_recent_clock (void)
{
  return recent_relative_time().tv_sec;
}

/* Starting point of packet process function. */
int
bgp_read (struct thread *thread)
{

  
    //zlog_debug (" We got a message");



  int ret;
  u_char type = 0;
  u_char root_cause_id = 0;
  struct peer *peer;
  bgp_size_t size;
  char notify_data_length[2];

  /* Yes first of all get peer pointer. */
  peer = THREAD_ARG (thread);
  peer->t_read = NULL;
  //if (peer->packet_size == 0)// we get error because we have not callled BGP_READ_ON and 
  //there in nothing for peer->packet_size
      //zlog_debug ("%s We should get nothingor error as we have not called BGP_READ_ON yet ", "peer->packet_size");



  /* For non-blocking IO check. */
  if (peer->status == Connect)
    {
      bgp_connect_check (peer);
      goto done;
    }
  else
    {
      if (peer->fd < 0)
	{
	  zlog_err ("bgp_read peer's fd is negative value %d", peer->fd);
	  return -1;
	}

      BGP_READ_ON (peer->t_read, bgp_read, peer->fd);
    }
    //zlog_debug (" We got a message from %s", peer->host);

  /* Read packet header to determine type of the packet */
  if (peer->packet_size == 0)
    peer->packet_size = BGP_HEADER_SIZE;

  if (stream_get_endp (peer->ibuf) < BGP_HEADER_SIZE)
    {
    // zlog_debug (" I am in the line which stream_get_endp (peer->ibuf) < BGP_HEADER_SIZE");

      ret = bgp_read_packet (peer);

      /* Header read error or partial read packet. */
      if (ret < 0) 
      {     // zlog_debug (" Header read error or partial read packet and finished!!");


	goto done;
}

   // zlog_debug (" lets check the message type and size %s", peer->host);

      /* Get size and type. */
      stream_forward_getp (peer->ibuf, BGP_MARKER_SIZE);
     // zlog_debug (" lets check the message type and size after stream_forward_getp %s", peer->host);

      memcpy (notify_data_length, stream_pnt (peer->ibuf), 2);

     // zlog_debug (" lets check the message type and size after memcpy %s", peer->host);

      size = stream_getw (peer->ibuf);
      type = stream_getc (peer->ibuf);

      //zlog_debug (" we got type and size %s", peer->host);


  char result22[50]; 
  sprintf(result22, "%u", type);

  //zlog_debug ("%s the first Type", result22);




  char result32[50]; 
  sprintf(result32, "%u", size);


  //zlog_debug ("%s the first size is ", result32 );


      // if (type == BGP_MSG_FIZZLE)
      //   zlog_debug (" ...........=====this is a FIZZLE message from....======  %s", peer->host);
      
      // if (type == BGP_MSG_CONVERGENCE)
      //   zlog_debug (" ...........=====this is a CONVERGENCE message from....======  %s", peer->host);
      



      // if (type == BGP_MSG_FIZZLE)
      // {
      //   zlog_debug (" ...........=====this is a FIZZLE message from....======  %s", peer->host);
      //   bgp_fizzle_receive (peer, size);
        
      //   //   /* Clear input buffer. */
      //   // peer->packet_size = 0;
      //   // if (peer->ibuf)
      //   //   stream_reset (peer->ibuf);
      //   return 0;
      // }







      if (BGP_DEBUG (normal, NORMAL) && type != 2 && type != 0)
      {      


       // zlog_debug ("We got error due to type != 2 && type != 0 " );


	zlog_debug ("%s rcv message type %d, length (excl. header) %d",
		   peer->host, type, size - BGP_HEADER_SIZE);
}

      /* Marker check */
      if (((type == BGP_MSG_OPEN) || (type == BGP_MSG_KEEPALIVE))
	  && ! bgp_marker_all_one (peer->ibuf, BGP_MARKER_SIZE))
	{

   // zlog_debug ("We got error here " );



	  bgp_notify_send (peer,
			   BGP_NOTIFY_HEADER_ERR, 
			   BGP_NOTIFY_HEADER_NOT_SYNC);
	  goto done;
	}



      /* BGP type check. */
      if (type != BGP_MSG_OPEN && type != BGP_MSG_UPDATE 
	  && type != BGP_MSG_NOTIFY && type != BGP_MSG_KEEPALIVE 
	  && type != BGP_MSG_ROUTE_REFRESH_NEW
	  && type != BGP_MSG_ROUTE_REFRESH_OLD
	  && type != BGP_MSG_CAPABILITY && type != BGP_MSG_FIZZLE
    && type != BGP_MSG_CONVERGENCE)
	{
	  if (BGP_DEBUG (normal, NORMAL))
	    plog_debug (peer->log,
		      "%s unknown message type 0x%02x",
		      peer->host, type);
	  bgp_notify_send_with_data (peer,
				     BGP_NOTIFY_HEADER_ERR,
			 	     BGP_NOTIFY_HEADER_BAD_MESTYPE,
				     &type, 1);
	  goto done;
	}

   //zlog_debug (" lets check the Mimimum packet length  %s", peer->host);



      /* Mimimum packet length check. */
      if ((size < BGP_HEADER_SIZE)
	  || (size > BGP_MAX_PACKET_SIZE)
	  || (type == BGP_MSG_OPEN && size < BGP_MSG_OPEN_MIN_SIZE)
	  || (type == BGP_MSG_UPDATE && size < BGP_MSG_UPDATE_MIN_SIZE)
	  || (type == BGP_MSG_NOTIFY && size < BGP_MSG_NOTIFY_MIN_SIZE)
	  || (type == BGP_MSG_KEEPALIVE && size != BGP_MSG_KEEPALIVE_MIN_SIZE)
	  || (type == BGP_MSG_ROUTE_REFRESH_NEW && size < BGP_MSG_ROUTE_REFRESH_MIN_SIZE)
	  || (type == BGP_MSG_ROUTE_REFRESH_OLD && size < BGP_MSG_ROUTE_REFRESH_MIN_SIZE)
	  || (type == BGP_MSG_CAPABILITY && size < BGP_MSG_CAPABILITY_MIN_SIZE)
    )
	{
	  if (BGP_DEBUG (normal, NORMAL))
	    plog_debug (peer->log,
		      "%s bad message length - %d for %s",
		      peer->host, size, 
		      type == 128 ? "ROUTE-REFRESH" :
		      bgp_type_str[(int) type]);
   // zlog_debug (" we are going to call bgp_notify_send_with_data");
	  bgp_notify_send_with_data (peer,
				     BGP_NOTIFY_HEADER_ERR,
			  	     BGP_NOTIFY_HEADER_BAD_MESLEN,
				     (u_char *) notify_data_length, 2);
	  goto done;
	}

      /* Adjust size to message length. */
      peer->packet_size = size;
    }


   // zlog_debug (" lets bgp_read_packet  %s", peer->host);

  ret = bgp_read_packet (peer);
  if (ret < 0) 
    goto done;

  /* Get size and type again. */
  size = stream_getw_from (peer->ibuf, BGP_MARKER_SIZE);
  type = stream_getc_from (peer->ibuf, BGP_MARKER_SIZE + 2);


  char result2[50]; 
  sprintf(result2, "%u", type);

 // zlog_debug ("%s this is the type of the  received packet", result2);




  char result[50]; 
  sprintf(result, "%u", size);


 //zlog_debug ("%s this is the size int  of received packet ", result );


  /* BGP packet dump function. */
  bgp_dump_packet (peer, type, peer->ibuf);
  
  size = (peer->packet_size - BGP_HEADER_SIZE);

  /* Read rest of the packet and call each sort of packet routine */

    //zlog_debug ("we are going to check the type in switch " );



  switch (type) 
    {
    case BGP_MSG_OPEN:
      peer->open_in++;
      bgp_open_receive (peer, size); /* XXX return value ignored! */
      break;

    case BGP_MSG_CONVERGENCE:
      //zlog_debug ("%s The type of received packet is BGP_MSG_CONVERGENCE, let's pars it", "horraaaaaaa........");
      bgp_convergence_receive (peer, size);
      break;
    case BGP_MSG_UPDATE:
      //zlog_debug ("%s we received an new update packet ", "!!!!!!!!!!!!!!!!!!!!" );

      peer->readtime = bgp_recent_clock ();
      bgp_update_receive (peer, size);
      break;
    case BGP_MSG_FIZZLE:
      //zlog_debug ("%s The type of received packet is FIZZLE, let's pars it", "horraaaaaaa........");
      bgp_fizzle_receive (peer, size);
      break;
    case BGP_MSG_NOTIFY:
      bgp_notify_receive (peer, size);
      break;
    case BGP_MSG_KEEPALIVE:

      //zlog_debug ("%s The type of received packet is BGP_MSG_KEEPALIVE, let's pars it", "horraaaaaaa........");

      peer->readtime = bgp_recent_clock ();
      bgp_keepalive_receive (peer, size);
      break;
    case BGP_MSG_ROUTE_REFRESH_NEW:
    case BGP_MSG_ROUTE_REFRESH_OLD:
      peer->refresh_in++;
      bgp_route_refresh_receive (peer, size);
      break;
    case BGP_MSG_CAPABILITY:
      peer->dynamic_cap_in++;
      bgp_capability_receive (peer, size);
      break;
    }

  /* Clear input buffer. */
  peer->packet_size = 0;
  if (peer->ibuf)
    stream_reset (peer->ibuf);

 done:
  if (CHECK_FLAG (peer->sflags, PEER_STATUS_ACCEPT_PEER))
    {
      if (BGP_DEBUG (events, EVENTS))
	zlog_debug ("%s [Event] Accepting BGP peer delete", peer->host);
      peer_delete (peer);
    }
  return 0;
}
